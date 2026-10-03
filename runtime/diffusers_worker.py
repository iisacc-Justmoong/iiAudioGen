"""Local-only AudioLDM inference worker. stdout is reserved for framed binary IPC."""

import json
import math
import os
from pathlib import Path
import struct
import sys

INPUT = sys.stdin.buffer
OUTPUT = sys.stdout.buffer
sys.stdout = sys.stderr
os.environ["HF_HUB_OFFLINE"] = "1"
os.environ["TRANSFORMERS_OFFLINE"] = "1"
os.environ["HF_HUB_DISABLE_PROGRESS_BARS"] = "1"
os.environ["TOKENIZERS_PARALLELISM"] = "false"

INVALID_REQUEST, UNAVAILABLE, GENERATION_FAILED, INVALID_MODEL, LOAD_FAILED, NOT_LOADED = range(6)
MAX_REQUEST = 64 * 1024


def read_exact(count):
    chunks = bytearray()
    while len(chunks) < count:
        part = INPUT.read(count - len(chunks))
        if not part:
            raise EOFError
        chunks.extend(part)
    return bytes(chunks)


def send(payload):
    OUTPUT.write(struct.pack("<Q", len(payload)))
    OUTPUT.write(payload)
    OUTPUT.flush()


def error(code, message):
    send(b"\x01" + struct.pack("<I", code) + str(message)[:2048].encode("utf-8", errors="replace"))


class Runtime:
    def __init__(self, device, steps):
        if device not in ("cpu", "mps", "cuda") or not 1 <= steps <= 1000:
            raise ValueError("Invalid runtime device or inference step count")
        self.device = device
        self.steps = steps
        self.pipeline = None
        self.torch = None

    def load(self, model_path):
        path = Path(model_path).resolve(strict=True)
        if not path.is_dir():
            raise ValueError("AudioLDM requires a local Diffusers model directory")
        manifest = json.loads((path / "model_index.json").read_text(encoding="utf-8"))
        pipeline_name = manifest.get("_class_name")
        if pipeline_name not in ("AudioLDMPipeline", "AudioLDM2Pipeline"):
            raise ValueError("Supported model classes: AudioLDMPipeline, AudioLDM2Pipeline")
        audioldm2_components = {
            ("audioldm2", "AudioLDM2ProjectionModel"),
            ("audioldm2", "AudioLDM2UNet2DConditionModel"),
        }
        for name, component in manifest.items():
            if name.startswith("_"):
                continue
            if not isinstance(component, list) or len(component) != 2 or \
                    not all(value is None or isinstance(value, str) for value in component):
                raise ValueError("Invalid model component declaration")
            if component[0] not in ("diffusers", "transformers", None) and \
                    not (pipeline_name == "AudioLDM2Pipeline" and tuple(component) in audioldm2_components):
                raise ValueError("Only built-in Diffusers/Transformers model components are supported")
            if component[0] is None and component[1] is not None:
                raise ValueError("An absent component must not declare a custom class")
            if component[0] and (path / name / f"{component[0]}.py").exists():
                raise ValueError("Local Python component overrides are not supported")

        import torch
        import diffusers

        torch.set_num_threads(4)
        if self.device == "mps" and not torch.backends.mps.is_available():
            raise RuntimeError("MPS is not available")
        if self.device == "cuda" and not torch.cuda.is_available():
            raise RuntimeError("CUDA is not available")
        pipeline_type = getattr(diffusers, pipeline_name)
        candidate = pipeline_type.from_pretrained(
            str(path), local_files_only=True, use_safetensors=True, torch_dtype=torch.float32
        )
        candidate.to(self.device)
        candidate.set_progress_bar_config(disable=True)
        if int(candidate.vocoder.config.sampling_rate) <= 0:
            raise ValueError("Model has an invalid native audio sample rate")
        # Failed replacements leave the previous pipeline intact.
        self.pipeline = candidate
        self.torch = torch

    def generate(self, payload):
        if self.pipeline is None:
            error(NOT_LOADED, "Load an audio model before generation")
            return
        header_size = struct.calcsize("<dIHBQ")
        if len(payload) <= header_size:
            raise ValueError("Generation request is truncated")
        duration, sample_rate, channels, has_seed, seed = struct.unpack("<dIHBQ", payload[:header_size])
        prompt = payload[header_size:].decode("utf-8", errors="strict")
        if not prompt.strip() or not math.isfinite(duration) or not 0 < duration <= 60:
            raise ValueError("Prompt must not be blank; duration must be in (0, 60] seconds")
        if not 8000 <= sample_rate <= 192000 or channels not in (1, 2) or has_seed not in (0, 1):
            raise ValueError("Output must use 8000..192000 Hz and one or two channels")

        import numpy as np
        from scipy.signal import resample_poly

        generator = self.torch.Generator(device="cpu").manual_seed(seed) if has_seed else None
        with self.torch.inference_mode():
            output = self.pipeline(
                prompt=prompt, audio_length_in_s=duration,
                num_inference_steps=self.steps, generator=generator,
                num_waveforms_per_prompt=1, output_type="np"
            ).audios
        samples = np.asarray(output[0], dtype=np.float32)
        if samples.ndim != 1 or samples.size == 0 or not np.isfinite(samples).all():
            raise RuntimeError("The model returned invalid mono PCM")
        native_rate = int(self.pipeline.vocoder.config.sampling_rate)
        if sample_rate != native_rate:
            divisor = math.gcd(sample_rate, native_rate)
            samples = resample_poly(samples, sample_rate // divisor, native_rate // divisor)
        frames = max(1, math.floor(duration * sample_rate + 0.5))
        if samples.size < frames:
            if frames - samples.size > math.ceil(sample_rate / native_rate) + 1:
                raise RuntimeError("The model returned less audio than requested")
            samples = np.pad(samples, (0, frames - samples.size))
        samples = samples[:frames]
        if channels == 2:
            # AudioLDM is mono. Stereo output duplicates the generated channel.
            samples = np.repeat(samples[:, None], 2, axis=1)
        samples = np.asarray(samples, dtype="<f4", order="C")
        if not np.isfinite(samples).all():
            raise RuntimeError("Audio conversion produced non-finite PCM")
        send(b"\x00" + struct.pack("<IHQ", sample_rate, channels, frames) + samples.tobytes())


def main():
    runtime = Runtime(sys.argv[1], int(sys.argv[2]))
    while True:
        try:
            length = struct.unpack("<Q", read_exact(8))[0]
            if not 1 <= length <= MAX_REQUEST:
                return 2
            request = read_exact(length)
        except EOFError:
            return 0
        operation, payload = request[0], request[1:]
        try:
            if operation == 1:
                runtime.load(payload.decode("utf-8", errors="strict"))
                send(b"\x00")
            elif operation == 2:
                runtime.generate(payload)
            else:
                error(INVALID_REQUEST, "Unknown operation")
        except (ModuleNotFoundError, ImportError) as exc:
            error(UNAVAILABLE, f"Install runtime/requirements.txt in the configured Python environment: {exc}")
        except (ValueError, FileNotFoundError, NotADirectoryError, json.JSONDecodeError) as exc:
            error(INVALID_MODEL if operation == 1 else INVALID_REQUEST, exc)
        except Exception as exc:
            error(LOAD_FAILED if operation == 1 else GENERATION_FAILED, exc)


if __name__ == "__main__":
    raise SystemExit(main())

import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

WORKER_PATH = Path(sys.argv.pop(1))
TEST_ROOT = Path(sys.argv.pop(1))
spec = importlib.util.spec_from_file_location("audio_worker", WORKER_PATH)
worker = importlib.util.module_from_spec(spec)
with patch.dict(os.environ), patch("sys.stdout"):
    spec.loader.exec_module(worker)


class WorkerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(dir=TEST_ROOT)
        self.path = Path(self.directory.name)
        (self.path / "model_index.json").write_text(json.dumps({"_class_name": "AudioLDMPipeline"}))
        self.runtime = worker.Runtime("cpu", 20)
        self.arguments = None
        self.device = None
        owner = self

        class Pipeline:
            vocoder = types.SimpleNamespace(config=types.SimpleNamespace(sampling_rate=16000))
            @classmethod
            def from_pretrained(cls, path, **options):
                owner.arguments = (path, options)
                return cls()
            def to(self, device):
                owner.device = device
            def set_progress_bar_config(self, **options):
                pass

        self.pipeline = Pipeline
        self.torch = types.SimpleNamespace(float32="float32", set_num_threads=lambda count: None)
        self.modules = {"torch": self.torch,
                        "diffusers": types.SimpleNamespace(AudioLDMPipeline=Pipeline, AudioLDM2Pipeline=Pipeline)}
        worker.OUTPUT = io.BytesIO()

    def tearDown(self):
        self.directory.cleanup()

    def test_local_safe_loading(self):
        with patch.dict(sys.modules, self.modules):
            self.runtime.load(str(self.path))
        self.assertEqual(self.device, "cpu")
        self.assertEqual(self.arguments[1], {
            "local_files_only": True, "use_safetensors": True, "torch_dtype": "float32"})
        self.assertIsInstance(self.runtime.pipeline, self.pipeline)

    def test_failed_replacement_preserves_model(self):
        prior = object()
        self.runtime.pipeline = prior
        with patch.object(self.pipeline, "from_pretrained", side_effect=RuntimeError("bad weights")), \
                patch.dict(sys.modules, self.modules):
            with self.assertRaises(RuntimeError):
                self.runtime.load(str(self.path))
        self.assertIs(self.runtime.pipeline, prior)

    def test_unsupported_model_class(self):
        (self.path / "model_index.json").write_text(json.dumps({"_class_name": "CustomPipeline"}))
        with self.assertRaises(ValueError):
            self.runtime.load(str(self.path))

    def test_generation_requires_model(self):
        self.runtime.generate(b"")
        response = worker.OUTPUT.getvalue()
        self.assertEqual(response[8], 1)
        self.assertEqual(struct.unpack("<I", response[9:13])[0], worker.NOT_LOADED)

    def test_custom_components_are_rejected(self):
        (self.path / "model_index.json").write_text(json.dumps({
            "_class_name": "AudioLDM2Pipeline", "unet": ["custom_module", "CustomUNet"]}))
        with self.assertRaises(ValueError):
            self.runtime.load(str(self.path))

    def test_builtin_audioldm2_components_are_supported(self):
        (self.path / "model_index.json").write_text(json.dumps({
            "_class_name": "AudioLDM2Pipeline",
            "projection_model": ["audioldm2", "AudioLDM2ProjectionModel"],
            "unet": ["audioldm2", "AudioLDM2UNet2DConditionModel"]}))
        with patch.dict(sys.modules, self.modules):
            self.runtime.load(str(self.path))
        self.assertIsInstance(self.runtime.pipeline, self.pipeline)

    def test_local_python_component_override_is_rejected(self):
        (self.path / "unet").mkdir()
        (self.path / "unet" / "diffusers.py").write_text("raise RuntimeError('must not run')")
        (self.path / "model_index.json").write_text(json.dumps({
            "_class_name": "AudioLDMPipeline", "unet": ["diffusers", "UNet2DConditionModel"]}))
        with self.assertRaises(ValueError):
            self.runtime.load(str(self.path))

    def test_invalid_generation_requests(self):
        self.runtime.pipeline = object()
        for duration, rate, channels in [(61, 16000, 1), (1, 0, 1), (1, 16000, 3)]:
            with self.subTest(duration=duration, rate=rate, channels=channels):
                payload = struct.pack("<dIHBQ", duration, rate, channels, 1, 42) + b"Rain"
                with self.assertRaises(ValueError):
                    self.runtime.generate(payload)

    def test_model_errors_are_framed(self):
        payload = b"\x01" + str(self.path / "missing").encode()
        worker.INPUT = io.BytesIO(struct.pack("<Q", len(payload)) + payload)
        with patch.object(sys, "argv", ["worker", "cpu", "20"]):
            self.assertEqual(worker.main(), 0)
        response = worker.OUTPUT.getvalue()
        self.assertEqual(struct.unpack("<Q", response[:8])[0], len(response) - 8)
        self.assertEqual(struct.unpack("<I", response[9:13])[0], worker.INVALID_MODEL)


if __name__ == "__main__":
    unittest.main()

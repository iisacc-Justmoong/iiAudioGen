# iiAudioGen

[Windows build and execution](docs/WindowsBuildAndRun.md)

Package release: **0.3.1** (2026-10-19). This patch packages the current
source snapshot; build, test and installation evidence is recorded in the SDK
patch-release report. Earlier versioned feature notes remain historical.


C++23 SDK for local, prompt-conditioned audio generation, version 0.3.1. The default `Generator` uses an isolated, persistent Python worker running the official Diffusers `AudioLDMPipeline` or `AudioLDM2Pipeline`. The C++ library and public API have no Qt dependency.

A local model directory is loaded once with `loadModel()`. `generate()` passes the text prompt, duration and optional seed to the loaded pipeline, validates the returned PCM, resamples it to the requested rate and returns interleaved floating-point samples. The SDK does not download models during loading or inference.

`Generator::loadModel()` rejects `.onnx` model files, regardless of extension case, before invoking a backend. GGUF, safetensors and other paths are forwarded to the selected backend, which must validate actual format and architecture support. The default Diffusers backend accepts local model directories and loads weights with `use_safetensors=True`; forwarding a GGUF path does not mean that this backend implements GGUF inference.

Fixed project assets belong under `resources/`; no model weights are bundled.

## Requirements and build

The library requires CMake 3.24 or later, C++23 `std::expected`, and native process/thread APIs. It does not discover or link another iiSDK module. Tests additionally require Python 3.10 or later. Actual inference requires a Python environment containing [runtime/requirements.txt](runtime/requirements.txt); Python 3.12 was used for local verification.

```sh
python3.12 -m venv build/python
build/python/bin/python -m pip install -r runtime/requirements.txt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DPython3_EXECUTABLE="$PWD/build/python/bin/python" \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/stage"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release
```

All build and verification outputs stay under `build/`. Shared libraries are enabled by default; use `-DBUILD_SHARED_LIBS=OFF` for a static library. Configure `-DBUILD_TESTING=OFF` to build the C++ library without finding Python at build time. The standalone default install prefix is `~/.local/SDK/iiAudioGen` when no prefix is provided.

The Python worker source is embedded in the library. Installed consumers need neither the SDK source tree nor a separately located worker script. Python and its ML dependencies remain host-managed runtime dependencies.

Use the pinned requirements in a dedicated environment. AudioLDM2's GPT-2 generation path is incompatible with the tested Transformers 5 stack; this runtime pins Transformers 4.48.3 and a compatible Diffusers/Hugging Face Hub combination. Upgrading these packages requires rerunning the real-model acceptance test.

## C++ API

```cpp
#include <iiAudioGen/Diffusers.h>

iiAudioGen::DiffusersOptions runtime;
runtime.pythonExecutable = "/absolute/path/to/python";
runtime.device = "cpu";
runtime.inferenceSteps = 50;

iiAudioGen::Generator generator{iiAudioGen::createDiffusersBackend(runtime)};
auto loaded = generator.loadModel("/absolute/path/to/local/audio-model");
if (loaded) {
    iiAudioGen::GenerationRequest request{
        "Gentle rain falling on leaves in a quiet forest.",
        5.0, 44100, 1, 42
    };
    auto audio = generator.generate(request);
    // Check audio before reading audio->samples, audio->sampleRate and audio->channels.
}
```

`Generator{}` selects the same concrete backend with `python3`, CPU, 50 steps and a five-minute timeout. Set `DiffusersOptions` explicitly when using a dedicated runtime. `Generator{nullptr}` creates an unconfigured generator. Custom `Backend` implementations remain injectable.

`loadModel()` returns `std::expected<void, Error>`. `generate()` returns `std::expected<GenerationResult, Error>`. Generation before loading returns `ModelNotLoaded`. Missing runtime dependencies return `BackendUnavailable`; malformed or unsupported models, loading failures, invalid requests and inference failures have separate errors.

The concrete backend supports local Diffusers directories whose `model_index.json` declares `AudioLDMPipeline` or `AudioLDM2Pipeline`, with Safetensors weights and all tokenizer/configuration components present. Standalone checkpoint files, Hub identifiers, custom pipelines and arbitrary architectures are not supported by this backend.

Output duration is limited to 60 seconds, sample rates to 8000..192000 Hz and channel counts to one or two. AudioLDM produces mono audio; two-channel output duplicates that generated channel. Resampling changes the output format and does not increase the model's native bandwidth. CPU, MPS and CUDA are selectable; local physical verification used CPU on macOS arm64.

## CLI

`iiaudio-generate` is a separately configured consumer application. It links
iiAudioGen and iiFileProvider independently and publishes 16-bit PCM WAV through
iiFileProvider. Build and install the two SDK packages first, then configure the
tool; it is no longer installed by the library's CMake project:

```sh
cmake -S tools -B build/tools \
  -DiiAudioGen_DIR="$PWD/build/stage/lib/cmake/iiAudioGen" \
  -DiiFileProviderNative_DIR="$HOME/.local/SDK/iiFileProvider/lib/cmake/iiFileProviderNative"
cmake --build build/tools --config Release
build/tools/iiaudio-generate \
  --model "$PWD/resources/models/audioldm2" \
  --python "$PWD/build/python/bin/python" \
  --prompt "Gentle rain falling on leaves in a quiet forest." \
  --output "$PWD/resources/rain.wav" \
  --device cpu --steps 50 --duration 5 --rate 44100 --channels 1 --seed 42
```

The tool project also owns the generated-file publication test and optional
real-model acceptance test. Installing the library alone does not install the
CLI; install the separately built tool project when that executable is needed.

Prepare the model separately using the provider's normal download procedure. Model weights are not included in this repository or installed package. The reference CVSSP AudioLDM models used for development declare **CC-BY-NC-SA-4.0**; consult their [AudioLDM](https://huggingface.co/cvssp/audioldm-s-full-v2) and [AudioLDM2](https://huggingface.co/cvssp/audioldm2) model cards before selecting a model for commercial work.

## Installed package

```cmake
find_package(iiAudioGen 0.3 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE iiAudioGen::iiAudioGen)
```

```sh
cmake -S tests/consumer -B build/consumer/build -DCMAKE_BUILD_TYPE=Release \
  -DiiAudioGen_DIR="$PWD/build/stage/lib/cmake/iiAudioGen"
cmake --build build/consumer/build --config Release
ctest --test-dir build/consumer/build -C Release --output-on-failure
```

Version 0.3.0 uses shared-library ABI 0.3. Rebuild consumers against the new package. Public headers are `<iiAudioGen/iiAudioGen.h>` and `<iiAudioGen/Diffusers.h>`.

## Verification

Normal CTest covers request/model contracts, local-only safe loading, failed model replacement, IPC framing, PCM validation, worker crashes, timeouts and recovery. Protocol fixtures do not prove model inference.

For an explicit real-model acceptance test, configure:

```sh
cmake -S . -B build \
  -DIIAUDIOGEN_TEST_MODEL="$PWD/build/models/audioldm2" \
  -DIIAUDIOGEN_TEST_PYTHON="$PWD/build/python/bin/python"
cmake --build build --config Release
ctest --test-dir build -C Release -R '^iiAudioGen.real_inference$' --output-on-failure
```

That test loads real weights, generates three clips in one retained runtime, checks seed repeatability and prompt-dependent output, and writes `build/real-inference/rain.wav` and `bells.wav`. It does not grade perceptual audio quality. See [docs/Architecture.md](docs/Architecture.md) and [docs/Verification.md](docs/Verification.md) for implementation boundaries and recorded results.

## License

SPDX-License-Identifier: MIT

The original code and documentation in this repository are licensed under the
[MIT License](LICENSE). Third-party code, libraries, fonts, tools, and model
weights retain their respective licenses.

<!-- workspace-docs:readme:start -->
## Documentation entry points

- Human overview: this English `README.md`.
- Agent instructions: [AGENT.md](AGENT.md).
- Human documentation: [documentation index](docs/README.md).
- Structured agent documentation: [agent index](docs/agent-index.json).
<!-- workspace-docs:readme:end -->

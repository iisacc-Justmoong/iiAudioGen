#include <iiAudioGen/Diffusers.h>

#include <iostream>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const std::filesystem::path root{argv[2]};
    iiAudioGen::DiffusersOptions options;
    options.pythonExecutable = argv[1];
    options.timeout = std::chrono::seconds{2};
    iiAudioGen::Generator generator{iiAudioGen::createDiffusersBackend(options)};
    const iiAudioGen::GenerationRequest request{"Rain; $(echo literal)\n\"quoted\"", 0.001, 8000, 2, 42};
    const auto unloaded = generator.generate(request);
    check(!unloaded && unloaded.error().code == iiAudioGen::ErrorCode::ModelNotLoaded,
          "Generation before loading must fail");
    check(generator.loadModel(root / "ok").has_value(), "A complete runtime acknowledgment must load the model");
    const auto audio = generator.generate(request);
    check(audio && audio->samples.size() == 16 && audio->sampleRate == 8000 && audio->channels == 2 &&
              audio->samples.front() == 0.25F, "Framed PCM must decode to the requested output shape");
    auto invalid = request;
    invalid.durationSeconds = 61;
    const auto rejected = generator.generate(invalid);
    check(!rejected && rejected.error().code == iiAudioGen::ErrorCode::InvalidRequest,
          "Unsupported generation dimensions must fail before IPC");
    const auto replacement = generator.loadModel(root / "error");
    check(!replacement && replacement.error().code == iiAudioGen::ErrorCode::ModelLoadFailed &&
              generator.generate(request).has_value(), "A failed model replacement must retain the prior runtime");

    for (const char* mode : {"badpcm", "badlength"}) {
        check(generator.loadModel(root / mode).has_value(), "Protocol fixture load must succeed");
        const auto bad = generator.generate(request);
        check(!bad && bad.error().code == iiAudioGen::ErrorCode::GenerationFailed,
              "Non-finite PCM and oversized response lengths must fail");
        const auto retired = generator.generate(request);
        check(!retired && retired.error().code == iiAudioGen::ErrorCode::ModelNotLoaded,
              "A broken runtime must lose its loaded state");
    }
    for (const char* mode : {"crash", "timeout"}) {
        const auto failed = generator.loadModel(root / mode);
        check(!failed && failed.error().code == iiAudioGen::ErrorCode::ModelLoadFailed,
              "Runtime crashes and timeouts must return errors");
        check(generator.loadModel(root / "ok").has_value(), "A retired runtime must restart on the next load");
    }
    options.pythonExecutable = root / "missing-python";
    const auto absent = iiAudioGen::Generator{iiAudioGen::createDiffusersBackend(options)}.loadModel(root / "ok");
    check(!absent && absent.error().code == iiAudioGen::ErrorCode::BackendUnavailable,
          "An unavailable executable must return BackendUnavailable");
    const auto defaultState = iiAudioGen::Generator{}.generate(request);
    check(!defaultState && defaultState.error().code == iiAudioGen::ErrorCode::ModelNotLoaded,
          "The default generator must expose the concrete runtime without starting it eagerly");
    return failures == 0 ? 0 : 1;
}

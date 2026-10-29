#include <iiAudioGen/Diffusers.h>
#include <memory>

class RecordingBackend final : public iiAudioGen::Backend {
public:
    std::filesystem::path path;
    iiAudioGen::ModelLoadOutcome loadModel(const std::filesystem::path& value) override { path = value; return {}; }
    iiAudioGen::GenerationOutcome generate(const iiAudioGen::GenerationRequest&) override { return iiAudioGen::GenerationResult{}; }
};

int main() {
    iiAudioGen::Generator generator{iiAudioGen::createDiffusersBackend()};
    const auto loaded = generator.loadModel(std::filesystem::path{});
    const auto legacy = generator.loadModel("model.onnx");
    const auto result = generator.generate({"A quiet forest"});
    const auto backend = std::make_shared<RecordingBackend>();
    iiAudioGen::Generator custom{backend};
    const auto gguf = custom.loadModel("model.gguf");
    return gguf.has_value() && backend->path == "model.gguf" && !loaded && loaded.error().code == iiAudioGen::ErrorCode::InvalidModel &&
                   !legacy && legacy.error().code == iiAudioGen::ErrorCode::InvalidModel &&
                   !result && result.error().code == iiAudioGen::ErrorCode::ModelNotLoaded ? 0 : 1;
}

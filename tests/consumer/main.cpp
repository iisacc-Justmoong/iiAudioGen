#include <iiAudioGen/Diffusers.h>

int main() {
    iiAudioGen::Generator generator{iiAudioGen::createDiffusersBackend()};
    const auto loaded = generator.loadModel(std::filesystem::path{});
    const auto result = generator.generate({"A quiet forest"});
    return !loaded && loaded.error().code == iiAudioGen::ErrorCode::InvalidModel &&
                   !result && result.error().code == iiAudioGen::ErrorCode::ModelNotLoaded ? 0 : 1;
}

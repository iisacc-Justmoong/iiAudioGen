#include <iiAudioGen/iiAudioGen.h>

int main() {
    iiAudioGen::Generator generator;
    const auto loaded = generator.loadModel(std::filesystem::path{"models/audio"});
    const auto result = generator.generate({"A quiet forest"});
    return !loaded && loaded.error().code == iiAudioGen::ErrorCode::BackendUnavailable &&
                   !result && result.error().code == iiAudioGen::ErrorCode::BackendUnavailable ? 0 : 1;
}

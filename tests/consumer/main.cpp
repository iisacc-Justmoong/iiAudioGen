#include <iiAudioGen/iiAudioGen.h>

int main() {
    const auto result = iiAudioGen::Generator{}.generate({"A quiet forest"});
    return !result && result.error().code == iiAudioGen::ErrorCode::BackendUnavailable ? 0 : 1;
}

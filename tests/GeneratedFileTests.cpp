#include "../tools/Wav.h"
#include <iiFileProviderNative.h>
#include <stdexcept>
#include <limits>
static void check(bool condition) { if (!condition) throw std::runtime_error("Generated audio publication failed"); }
int main() {
    namespace provider=iiFileProvider::native;
    provider::TemporaryDirectory root;
    const auto target=root.path()/"generated.wav";
    provider::File::write(target,"existing");
    iiAudioGen::GenerationResult audio;
    audio.sampleRate=48000;audio.channels=2;
    audio.samples={0.0f,1.0f,-1.0f,std::numeric_limits<float>::quiet_NaN()};
    check(!iiAudioGen::tools::writeWav(target,audio));
    check(provider::File::read(target)=="existing");
    audio.samples.back()=0.5f;
    check(iiAudioGen::tools::writeWav(target,audio));
    const auto bytes=provider::File::read(target);
    check(bytes.size()==52 && bytes.substr(0,4)=="RIFF" && bytes.substr(8,4)=="WAVE" && bytes.substr(36,4)=="data");
}

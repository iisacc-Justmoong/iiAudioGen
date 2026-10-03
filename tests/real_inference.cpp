#include <iiAudioGen/Diffusers.h>
#include "../tools/Wav.h"

#include <chrono>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    iiAudioGen::DiffusersOptions options;
    options.pythonExecutable = argv[2];
    options.inferenceSteps = 20;
    iiAudioGen::Generator generator{iiAudioGen::createDiffusersBackend(options)};
    auto loaded = generator.loadModel(argv[1]);
    if (!loaded) { std::cerr << loaded.error().message << '\n'; return 1; }
    iiAudioGen::GenerationRequest request{
        "Gentle rain falling on leaves in a quiet forest.", 3.0, 44100, 1, 42};
    const auto begin = std::chrono::steady_clock::now();
    auto first = generator.generate(request);
    if (!first) { std::cerr << first.error().message << '\n'; return 1; }
    auto repeated = generator.generate(request);
    if (!repeated) { std::cerr << repeated.error().message << '\n'; return 1; }
    request.prompt = "A church bell ringing repeatedly in the distance.";
    auto different = generator.generate(request);
    if (!different) { std::cerr << different.error().message << '\n'; return 1; }
    if (first->samples != repeated->samples || first->samples == different->samples ||
        first->samples.size() != 132300 || first->sampleRate != 44100 || first->channels != 1) {
        std::cerr << "Seed repeatability, prompt conditioning or output shape failed\n";
        return 1;
    }
    double energy = 0;
    for (auto sample : first->samples) energy += sample * sample;
    if (energy == 0 || !std::isfinite(energy)) return 1;
    const std::filesystem::path root{argv[3]};
    if (!iiAudioGen::tools::writeWav(root / "rain.wav", *first) ||
        !iiAudioGen::tools::writeWav(root / "bells.wav", *different)) return 1;
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    std::cout << "Real inference passed: 3 generations, reproducible seed, distinct prompts, "
              << "132300 frames at 44100 Hz, RMS=" << std::sqrt(energy / first->samples.size())
              << ", generation elapsed=" << seconds << " seconds\n";
    return 0;
}

#include <iiAudioGen/Diffusers.h>
#include "Wav.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <charconv>
#include <stdexcept>

namespace {
std::uint64_t unsignedValue(const std::string& value, std::uint64_t maximum) {
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed > maximum)
        throw std::runtime_error("Invalid numeric argument: " + value);
    return parsed;
}
} // namespace

int main(int argc, char** argv) {
    try {
        std::map<std::string, std::string> arguments;
        for (int index = 1; index < argc; index += 2) {
            if (index + 1 >= argc) throw std::runtime_error("Arguments must be supplied as --name value pairs");
            const std::string name{argv[index]};
            if (name != "--model" && name != "--prompt" && name != "--output" && name != "--python" &&
                name != "--device" && name != "--steps" && name != "--duration" && name != "--rate" &&
                name != "--channels" && name != "--seed") throw std::runtime_error("Unknown argument: " + name);
            if (!arguments.emplace(name, argv[index + 1]).second) throw std::runtime_error("Duplicate argument: " + name);
        }
        if (!arguments.contains("--model") || !arguments.contains("--prompt") || !arguments.contains("--output")) {
            std::cerr << "Usage: iiaudio-generate --model DIRECTORY --prompt TEXT --output FILE.wav "
                         "[--python EXECUTABLE] [--device cpu|mps|cuda] [--steps N] [--duration SECONDS] "
                         "[--rate HZ] [--channels 1|2] [--seed N]\n";
            return 2;
        }
        iiAudioGen::DiffusersOptions options;
        if (arguments.contains("--python")) options.pythonExecutable = arguments.at("--python");
        if (arguments.contains("--device")) options.device = arguments.at("--device");
        if (arguments.contains("--steps")) options.inferenceSteps = static_cast<std::uint32_t>(unsignedValue(arguments.at("--steps"), 1000));
        iiAudioGen::GenerationRequest request{arguments.at("--prompt")};
        if (arguments.contains("--duration")) {
            std::size_t consumed;
            request.durationSeconds = std::stod(arguments.at("--duration"), &consumed);
            if (consumed != arguments.at("--duration").size()) throw std::runtime_error("Invalid duration argument");
        }
        if (arguments.contains("--rate")) request.sampleRate = static_cast<std::uint32_t>(unsignedValue(arguments.at("--rate"), 192000));
        if (arguments.contains("--channels")) request.channels = static_cast<std::uint16_t>(unsignedValue(arguments.at("--channels"), 2));
        if (arguments.contains("--seed")) request.seed = unsignedValue(arguments.at("--seed"), std::numeric_limits<std::uint64_t>::max());
        iiAudioGen::Generator generator{iiAudioGen::createDiffusersBackend(options)};
        const auto loaded = generator.loadModel(arguments.at("--model"));
        if (!loaded) { std::cerr << "Model loading failed: " << loaded.error().message << '\n'; return 1; }
        const auto result = generator.generate(request);
        if (!result) { std::cerr << "Generation failed: " << result.error().message << '\n'; return 1; }
        if (!iiAudioGen::tools::writeWav(arguments.at("--output"), *result)) throw std::runtime_error("Could not write the output WAV");
        const auto frames = result->samples.size() / result->channels;
        std::cout << "Generated " << frames << " frames, " << result->sampleRate << " Hz, "
                  << result->channels << " channel(s), " << double(frames) / result->sampleRate
                  << " seconds: " << arguments.at("--output") << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}

#pragma once

#include <iiAudioGen/iiAudioGen.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace iiAudioGen::tools {

inline void number(std::ostream& stream, std::uint32_t value, int bytes) {
    for (int index = 0; index < bytes; ++index) stream.put(static_cast<char>((value >> (8 * index)) & 0xff));
}

inline bool writeWav(const std::filesystem::path& path, const GenerationResult& audio) {
    if (audio.samples.size() > (std::numeric_limits<std::uint32_t>::max() - 36) / 2 ||
        audio.channels == 0 || audio.sampleRate == 0 || audio.samples.size() % audio.channels != 0) return false;
    std::ofstream output(path, std::ios::binary);
    if (!output) return false;
    const auto size = static_cast<std::uint32_t>(audio.samples.size() * 2);
    output.write("RIFF", 4); number(output, 36 + size, 4); output.write("WAVEfmt ", 8);
    number(output, 16, 4); number(output, 1, 2); number(output, audio.channels, 2);
    number(output, audio.sampleRate, 4); number(output, audio.sampleRate * audio.channels * 2, 4);
    number(output, audio.channels * 2, 2); number(output, 16, 2);
    output.write("data", 4); number(output, size, 4);
    for (float sample : audio.samples) {
        if (!std::isfinite(sample)) return false;
        const auto value = static_cast<std::int16_t>(std::lround(std::clamp(sample, -1.0F, 1.0F) * 32767));
        number(output, static_cast<std::uint16_t>(value), 2);
    }
    output.close();
    return static_cast<bool>(output);
}

} // namespace iiAudioGen::tools

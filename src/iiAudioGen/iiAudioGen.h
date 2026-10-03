#pragma once

#include <iiAudioGen/Export.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace iiAudioGen {

struct GenerationRequest {
    std::string prompt;
    double durationSeconds{5.0};
    std::uint32_t sampleRate{44100};
    std::uint16_t channels{1};
    std::optional<std::uint64_t> seed;
};

struct GenerationResult {
    // Interleaved floating-point PCM: frame 0 channel 0, frame 0 channel 1, ...
    std::vector<float> samples;
    std::uint32_t sampleRate{0};
    std::uint16_t channels{0};
};

enum class ErrorCode {
    InvalidRequest,
    BackendUnavailable,
    GenerationFailed,
    InvalidModel,
    ModelLoadFailed,
    ModelNotLoaded,
};

struct Error {
    ErrorCode code;
    std::string message;
};

using GenerationOutcome = std::expected<GenerationResult, Error>;
using ModelLoadOutcome = std::expected<void, Error>;

class IIAUDIOGEN_EXPORT Backend {
public:
    virtual ~Backend() = default;
    [[nodiscard]] virtual ModelLoadOutcome loadModel(const std::filesystem::path& modelPath);
    [[nodiscard]] virtual GenerationOutcome generate(const GenerationRequest& request) = 0;
};

class IIAUDIOGEN_EXPORT Generator {
public:
    Generator();
    explicit Generator(std::shared_ptr<Backend> backend);
    [[nodiscard]] ModelLoadOutcome loadModel(const std::filesystem::path& modelPath);
    [[nodiscard]] GenerationOutcome generate(const GenerationRequest& request) const;

private:
    std::shared_ptr<Backend> backend_;
};

} // namespace iiAudioGen

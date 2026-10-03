#include <iiAudioGen/Diffusers.h>
#include "ChildProcess.h"

#include <bit>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <utility>

namespace iiAudioGen {
namespace {

using detail::Bytes;

void append(Bytes& target, std::uint64_t value, int bytes) {
    for (int index = 0; index < bytes; ++index) target.push_back((value >> (8 * index)) & 0xff);
}

std::uint64_t integer(const Bytes& data, std::size_t offset, int bytes) {
    std::uint64_t result = 0;
    for (int index = 0; index < bytes; ++index) result |= std::uint64_t{data[offset + index]} << (8 * index);
    return result;
}

class DiffusersBackend final : public Backend {
public:
    explicit DiffusersBackend(DiffusersOptions options) : options_(std::move(options)) {}

    ModelLoadOutcome loadModel(const std::filesystem::path& path) override {
        std::lock_guard guard{mutex_};
        if (options_.pythonExecutable.empty() || options_.inferenceSteps == 0 || options_.inferenceSteps > 1000 ||
            options_.timeout.count() <= 0 || options_.timeout > std::chrono::hours{24} ||
            (options_.device != "cpu" && options_.device != "mps" && options_.device != "cuda")) {
            return std::unexpected(Error{ErrorCode::BackendUnavailable, "Invalid Diffusers runtime options"});
        }
        std::error_code error;
        const auto absolute = std::filesystem::absolute(path, error);
        if (path.empty() || error || !std::filesystem::is_directory(absolute, error) || error) {
            return std::unexpected(Error{ErrorCode::InvalidModel, "AudioLDM requires an existing local model directory"});
        }
        const auto utf8 = absolute.u8string();
        if (utf8.size() > 16384 || utf8.find(char8_t{}) != std::u8string::npos) {
            return std::unexpected(Error{ErrorCode::InvalidModel, "Invalid model path"});
        }
        if (!process_.running()) {
            auto started = process_.start(options_.pythonExecutable, options_.device, options_.inferenceSteps);
            if (!started) return std::unexpected(Error{ErrorCode::BackendUnavailable, started.error()});
        }
        Bytes request{1};
        request.insert(request.end(), utf8.begin(), utf8.end());
        auto response = call(request, ErrorCode::ModelLoadFailed);
        if (!response) return std::unexpected(response.error());
        if (response->size() != 1) {
            process_.stop(); loaded_ = false;
            return std::unexpected(Error{ErrorCode::ModelLoadFailed, "Invalid model-load acknowledgment"});
        }
        loaded_ = true;
        return {};
    }

    GenerationOutcome generate(const GenerationRequest& request) override {
        std::lock_guard guard{mutex_};
        if (!loaded_) return std::unexpected(Error{ErrorCode::ModelNotLoaded, "Load an audio model before generation"});
        if (request.prompt.empty() || request.prompt.size() > 16384 ||
            !std::isfinite(request.durationSeconds) || request.durationSeconds <= 0 || request.durationSeconds > 60 ||
            request.sampleRate < 8000 || request.sampleRate > 192000 || request.channels < 1 || request.channels > 2) {
            return std::unexpected(Error{ErrorCode::InvalidRequest, "AudioLDM supports up to 60 seconds, 8000..192000 Hz and one or two channels"});
        }
        Bytes payload{2};
        append(payload, std::bit_cast<std::uint64_t>(request.durationSeconds), 8);
        append(payload, request.sampleRate, 4);
        append(payload, request.channels, 2);
        append(payload, request.seed.has_value(), 1);
        append(payload, request.seed.value_or(0), 8);
        payload.insert(payload.end(), request.prompt.begin(), request.prompt.end());
        auto response = call(payload, ErrorCode::GenerationFailed);
        if (!response) return std::unexpected(response.error());
        if (response->size() < 15) return invalidAudio();
        const auto rate = integer(*response, 1, 4), channels = integer(*response, 5, 2);
        const auto frames = integer(*response, 7, 8);
        const auto expectedFrames = static_cast<std::uint64_t>(std::max(1.0, std::round(request.durationSeconds * request.sampleRate)));
        if (rate != request.sampleRate || channels != request.channels || frames != expectedFrames ||
            frames > (128 * 1024 * 1024 - 15) / (channels * 4) || response->size() != 15 + frames * channels * 4) {
            return invalidAudio();
        }
        GenerationResult result{{}, static_cast<std::uint32_t>(rate), static_cast<std::uint16_t>(channels)};
        result.samples.reserve(frames * channels);
        for (std::size_t offset = 15; offset < response->size(); offset += 4) {
            const auto value = std::bit_cast<float>(static_cast<std::uint32_t>(integer(*response, offset, 4)));
            if (!std::isfinite(value)) return invalidAudio();
            result.samples.push_back(value);
        }
        return result;
    }

private:
    std::expected<Bytes, Error> call(const Bytes& request, ErrorCode failure) {
        auto result = process_.exchange(request, options_.timeout);
        if (!result) { loaded_ = false; return std::unexpected(Error{failure, result.error()}); }
        if ((*result)[0] == 0) return std::move(*result);
        if ((*result)[0] != 1 || result->size() < 5 || integer(*result, 1, 4) > static_cast<std::uint32_t>(ErrorCode::ModelNotLoaded)) {
            process_.stop(); loaded_ = false;
            return std::unexpected(Error{failure, "Python runtime returned an invalid error response"});
        }
        return std::unexpected(Error{static_cast<ErrorCode>(integer(*result, 1, 4)),
            std::string(result->begin() + 5, result->end())});
    }

    GenerationOutcome invalidAudio() {
        process_.stop(); loaded_ = false;
        return std::unexpected(Error{ErrorCode::GenerationFailed, "Python runtime returned invalid PCM metadata or samples"});
    }

    DiffusersOptions options_;
    detail::ChildProcess process_;
    bool loaded_{false};
    std::mutex mutex_;
};

} // namespace

std::shared_ptr<Backend> createDiffusersBackend(DiffusersOptions options) {
    return std::make_shared<DiffusersBackend>(std::move(options));
}

} // namespace iiAudioGen

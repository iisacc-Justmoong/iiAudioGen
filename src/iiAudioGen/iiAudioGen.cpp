#include <iiAudioGen/iiAudioGen.h>

#include <cmath>
#include <utility>

namespace iiAudioGen {

Generator::Generator(std::shared_ptr<Backend> backend)
    : backend_(std::move(backend)) {}

GenerationOutcome Generator::generate(const GenerationRequest& request) const {
    if (request.prompt.find_first_not_of(" \t\n\r\f\v") == std::string::npos) {
        return std::unexpected(Error{ErrorCode::InvalidRequest, "Prompt must not be blank"});
    }
    if (!std::isfinite(request.durationSeconds) || request.durationSeconds <= 0.0) {
        return std::unexpected(Error{ErrorCode::InvalidRequest,
                                     "Duration must be finite and greater than zero"});
    }
    if (request.sampleRate == 0 || request.channels == 0) {
        return std::unexpected(Error{ErrorCode::InvalidRequest,
                                     "Sample rate and channel count must be greater than zero"});
    }
    if (!backend_) {
        return std::unexpected(Error{ErrorCode::BackendUnavailable,
                                     "No audio generation backend is configured"});
    }
    return backend_->generate(request);
}

} // namespace iiAudioGen

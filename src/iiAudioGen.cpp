#include <iiAudioGen/iiAudioGen.h>
#include <iiAudioGen/Diffusers.h>

#include <cmath>
#include <utility>

namespace iiAudioGen {

ModelLoadOutcome Backend::loadModel(const std::filesystem::path&) {
    return std::unexpected(Error{ErrorCode::BackendUnavailable,
                                 "This backend does not implement audio model loading"});
}

Generator::Generator() : Generator(createDiffusersBackend()) {}

Generator::Generator(std::shared_ptr<Backend> backend)
    : backend_(std::move(backend)) {}

ModelLoadOutcome Generator::loadModel(const std::filesystem::path& modelPath) {
    if (modelPath.empty() ||
        modelPath.native().find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos) {
        return std::unexpected(Error{ErrorCode::InvalidModel,
                                     "Model path must not be empty or contain null characters"});
    }
    // Reject ONNX files before dispatch; backends validate all other formats.
    auto extension = modelPath.extension().u8string();
    for (auto& ch : extension) if (ch >= u8'A' && ch <= u8'Z') ch += u8'a' - u8'A';
    if (extension == u8".onnx") {
        std::error_code error;
        if (!std::filesystem::is_directory(modelPath, error)) {
            return std::unexpected(Error{ErrorCode::InvalidModel, "ONNX model files are not supported"});
        }
    }
    if (!backend_) {
        return std::unexpected(Error{ErrorCode::BackendUnavailable,
                                     "No audio generation backend is configured"});
    }
    return backend_->loadModel(modelPath);
}

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

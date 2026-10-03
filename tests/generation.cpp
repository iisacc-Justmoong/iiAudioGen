#include <iiAudioGen/iiAudioGen.h>

#include <iostream>
#include <limits>
#include <memory>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class RecordingBackend final : public iiAudioGen::Backend {
public:
    int calls{0};
    int modelCalls{0};
    std::filesystem::path receivedModel;
    std::filesystem::path modelDuringGeneration;
    iiAudioGen::ModelLoadOutcome modelOutcome;
    iiAudioGen::GenerationRequest received;
    iiAudioGen::GenerationOutcome outcome{iiAudioGen::GenerationResult{
        {0.25F, -0.25F, 0.5F, -0.5F}, 48000, 2}};

    iiAudioGen::ModelLoadOutcome loadModel(const std::filesystem::path& modelPath) override {
        ++modelCalls;
        receivedModel = modelPath;
        return modelOutcome;
    }

    iiAudioGen::GenerationOutcome generate(const iiAudioGen::GenerationRequest& request) override {
        ++calls;
        received = request;
        modelDuringGeneration = receivedModel;
        return outcome;
    }
};

void requestTest() {
    auto backend = std::make_shared<RecordingBackend>();
    iiAudioGen::Generator generator{backend};
    const iiAudioGen::GenerationRequest valid{"Rain on a window"};

    const auto expectInvalid = [&](const iiAudioGen::GenerationRequest& request) {
        const auto result = generator.generate(request);
        check(!result && result.error().code == iiAudioGen::ErrorCode::InvalidRequest,
              "Invalid requests must fail before backend execution");
    };

    auto request = valid;
    request.prompt.clear();
    expectInvalid(request);
    request.prompt = " \t\r\n\f\v";
    expectInvalid(request);
    request = valid;
    request.durationSeconds = 0.0;
    expectInvalid(request);
    request.durationSeconds = -1.0;
    expectInvalid(request);
    request.durationSeconds = std::numeric_limits<double>::infinity();
    expectInvalid(request);
    request.durationSeconds = std::numeric_limits<double>::quiet_NaN();
    expectInvalid(request);
    request = valid;
    request.sampleRate = 0;
    expectInvalid(request);
    request = valid;
    request.channels = 0;
    expectInvalid(request);
    check(backend->calls == 0, "Invalid requests must never reach the backend");

    const auto result = iiAudioGen::Generator{}.generate({});
    check(!result && result.error().code == iiAudioGen::ErrorCode::InvalidRequest,
          "Request validation must precede missing-backend detection");
}

void unavailableTest() {
    const auto result = iiAudioGen::Generator{nullptr}.generate({"Wind through trees"});
    check(!result && result.error().code == iiAudioGen::ErrorCode::BackendUnavailable,
          "An unconfigured generator must report BackendUnavailable");
    check(!result && !result.error().message.empty(), "Missing-backend errors must explain the failure");
}

void delegationTest() {
    auto backend = std::make_shared<RecordingBackend>();
    iiAudioGen::Generator generator{backend};
    const iiAudioGen::GenerationRequest request{"Ocean waves", 1.25, 48000, 2, 42};
    backend.reset();
    const auto result = generator.generate(request);
    check(result.has_value(), "An injected backend must be called and retained by the generator");
    if (result) {
        check(result->samples == std::vector<float>({0.25F, -0.25F, 0.5F, -0.5F}) &&
                  result->sampleRate == 48000 && result->channels == 2,
              "Backend audio results must be returned unchanged");
    }

    auto recording = std::make_shared<RecordingBackend>();
    const auto forwarded = iiAudioGen::Generator{recording}.generate(request);
    check(forwarded.has_value() && recording->calls == 1, "Each request must execute the backend once");
    check(recording->received.prompt == request.prompt &&
              recording->received.durationSeconds == request.durationSeconds &&
              recording->received.sampleRate == request.sampleRate &&
              recording->received.channels == request.channels &&
              recording->received.seed == request.seed,
          "All request fields must reach the backend unchanged");
}

void errorTest() {
    auto backend = std::make_shared<RecordingBackend>();
    backend->outcome = std::unexpected(iiAudioGen::Error{
        iiAudioGen::ErrorCode::GenerationFailed, "Test backend failure"});
    const auto result = iiAudioGen::Generator{backend}.generate({"Birdsong"});
    check(!result && result.error().code == iiAudioGen::ErrorCode::GenerationFailed &&
              result.error().message == "Test backend failure" && backend->calls == 1,
          "Backend failures must reach the caller unchanged");
}

void modelTest() {
    auto backend = std::make_shared<RecordingBackend>();
    iiAudioGen::Generator generator{backend};
    const auto empty = generator.loadModel({});
    check(!empty && empty.error().code == iiAudioGen::ErrorCode::InvalidModel &&
              backend->modelCalls == 0,
          "An empty model path must fail before backend execution");

    auto embeddedNull = std::filesystem::path{"models/audio"}.native();
    embeddedNull.push_back(std::filesystem::path::value_type{});
    embeddedNull += std::filesystem::path{"hidden-suffix"}.native();
    const auto invalid = generator.loadModel(std::filesystem::path{embeddedNull});
    check(!invalid && invalid.error().code == iiAudioGen::ErrorCode::InvalidModel &&
              backend->modelCalls == 0,
          "Embedded null characters must not reach a model loader");

    const auto noBackend = iiAudioGen::Generator{nullptr}.loadModel("models/audio");
    check(!noBackend && noBackend.error().code == iiAudioGen::ErrorCode::BackendUnavailable,
          "An unconfigured model slot must report BackendUnavailable");
    const auto emptyWithoutBackend = iiAudioGen::Generator{}.loadModel({});
    check(!emptyWithoutBackend && emptyWithoutBackend.error().code == iiAudioGen::ErrorCode::InvalidModel,
          "Model path validation must precede missing-backend detection");

    const std::filesystem::path directory{"models/audio model"};
    const std::filesystem::path checkpoint{"models/audio model/weights.safetensors"};
    const auto directoryResult = generator.loadModel(directory);
    check(directoryResult.has_value() && backend->modelCalls == 1 && backend->receivedModel == directory,
          "A model directory path must be forwarded once without rewriting");
    const auto checkpointResult = generator.loadModel(checkpoint);
    check(checkpointResult.has_value() && backend->modelCalls == 2 && backend->receivedModel == checkpoint,
          "A model file path must be forwarded once without format assumptions");
    const auto generated = generator.generate({"Rain on a window"});
    check(generated.has_value() && backend->calls == 1 && backend->modelDuringGeneration == checkpoint,
          "Loading and generation must use the same retained backend");

    backend->modelOutcome = std::unexpected(iiAudioGen::Error{
        iiAudioGen::ErrorCode::ModelLoadFailed, "Test model load failure"});
    const auto failed = generator.loadModel(checkpoint);
    check(!failed && failed.error().code == iiAudioGen::ErrorCode::ModelLoadFailed &&
              failed.error().message == "Test model load failure" && backend->modelCalls == 3,
          "Model loader failures must reach the caller unchanged");

    class GenerationOnlyBackend final : public iiAudioGen::Backend {
    public:
        iiAudioGen::GenerationOutcome generate(const iiAudioGen::GenerationRequest&) override {
            return iiAudioGen::GenerationResult{};
        }
    };
    const auto unsupported = iiAudioGen::Generator{std::make_shared<GenerationOnlyBackend>()}
                                 .loadModel(directory);
    check(!unsupported && unsupported.error().code == iiAudioGen::ErrorCode::BackendUnavailable,
          "A backend without a model loader must never claim successful loading");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        return 2;
    }
    const std::string_view name{argv[1]};
    if (name == "request") {
        requestTest();
    } else if (name == "unavailable") {
        unavailableTest();
    } else if (name == "delegation") {
        delegationTest();
    } else if (name == "error") {
        errorTest();
    } else if (name == "model") {
        modelTest();
    } else {
        return 2;
    }
    return failures == 0 ? 0 : 1;
}

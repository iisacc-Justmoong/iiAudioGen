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
    iiAudioGen::GenerationRequest received;
    iiAudioGen::GenerationOutcome outcome{iiAudioGen::GenerationResult{
        {0.25F, -0.25F, 0.5F, -0.5F}, 48000, 2}};

    iiAudioGen::GenerationOutcome generate(const iiAudioGen::GenerationRequest& request) override {
        ++calls;
        received = request;
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
    const auto result = iiAudioGen::Generator{}.generate({"Wind through trees"});
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
    } else {
        return 2;
    }
    return failures == 0 ? 0 : 1;
}

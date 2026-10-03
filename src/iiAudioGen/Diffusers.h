#pragma once

#include <iiAudioGen/iiAudioGen.h>

#include <chrono>

namespace iiAudioGen {

struct DiffusersOptions {
    std::filesystem::path pythonExecutable{"python3"};
    std::string device{"cpu"};
    std::uint32_t inferenceSteps{50};
    std::chrono::milliseconds timeout{std::chrono::minutes{5}};
};

// Creates an isolated, persistent runtime for local AudioLDM / AudioLDM2 directories.
[[nodiscard]] IIAUDIOGEN_EXPORT std::shared_ptr<Backend>
createDiffusersBackend(DiffusersOptions options = {});

} // namespace iiAudioGen

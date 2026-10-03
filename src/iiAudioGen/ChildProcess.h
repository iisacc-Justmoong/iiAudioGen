#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace iiAudioGen::detail {

using Bytes = std::vector<std::uint8_t>;

class ChildProcess {
public:
    ~ChildProcess();
    ChildProcess() = default;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    std::expected<void, std::string> start(const std::filesystem::path& executable,
                                         const std::string& device, std::uint32_t steps);
    std::expected<Bytes, std::string> exchange(std::span<const std::uint8_t> request,
                                              std::chrono::milliseconds timeout);
    void stop();
    bool running() const;

private:
    using Deadline = std::chrono::steady_clock::time_point;
    bool transfer(std::uint8_t* data, std::size_t size, bool writing, Deadline deadline);
#ifdef _WIN32
    HANDLE process_{nullptr};
    HANDLE input_{nullptr};
    HANDLE output_{nullptr};
#else
    int process_{-1};
    int socket_{-1};
#endif
};

} // namespace iiAudioGen::detail

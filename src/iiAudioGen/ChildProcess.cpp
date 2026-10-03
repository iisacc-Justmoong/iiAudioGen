#include "ChildProcess.h"
#include "WorkerSource.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace iiAudioGen::detail {

ChildProcess::~ChildProcess() { stop(); }

#ifdef _WIN32
namespace {
std::wstring widen(std::string_view text) {
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), count);
    return result;
}

std::wstring quote(const std::wstring& argument) {
    std::wstring result{L"\""};
    std::size_t slashes = 0;
    for (const auto character : argument) {
        if (character == L'\\') { ++slashes; continue; }
        result.append(character == L'"' ? 2 * slashes + 1 : slashes, L'\\');
        result += character;
        slashes = 0;
    }
    result.append(2 * slashes, L'\\');
    return result + L'"';
}
} // namespace
#endif

std::expected<void, std::string> ChildProcess::start(const std::filesystem::path& executable,
                                                   const std::string& device, std::uint32_t steps) {
    stop();
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE childInput = nullptr, childOutput = nullptr;
    if (!CreatePipe(&childInput, &input_, &security, 65536) ||
        !CreatePipe(&output_, &childOutput, &security, 65536)) {
        if (childInput) CloseHandle(childInput);
        if (childOutput) CloseHandle(childOutput);
        stop();
        return std::unexpected("Could not create Python IPC pipes");
    }
    SetHandleInformation(input_, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(output_, HANDLE_FLAG_INHERIT, 0);
    HANDLE log = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = childInput;
    startup.StartupInfo.hStdOutput = childOutput;
    startup.StartupInfo.hStdError = log;
    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
    std::vector<std::uint8_t> attributes(attributeSize);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    HANDLE inherited[]{childInput, childOutput, log};
    const bool initialized = InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attributeSize);
    const bool configured = initialized && UpdateProcThreadAttribute(startup.lpAttributeList, 0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr);
    auto command = quote(executable.wstring()) + L" -B -u -c " + quote(widen(workerSource)) +
                   L" " + quote(widen(device)) + L" " + std::to_wstring(steps);
    PROCESS_INFORMATION information{};
    const bool started = configured && CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
        &startup.StartupInfo, &information);
    if (initialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
    CloseHandle(childInput);
    CloseHandle(childOutput);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (!started) { stop(); return std::unexpected("Could not start the configured Python executable"); }
    CloseHandle(information.hThread);
    process_ = information.hProcess;
    return {};
#elif defined(__unix__) || defined(__APPLE__)
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
        return std::unexpected("Could not create Python IPC socket");
    }
    // Spawn descriptors must not alias stdio, even when the host closed stdin/stdout.
    for (int index = 0; index < 2; ++index) {
        const int descriptor = fcntl(pair[index], F_DUPFD_CLOEXEC, 3);
        if (descriptor < 0) { close(pair[0]); close(pair[1]); return std::unexpected("IPC descriptor failure"); }
        close(pair[index]);
        pair[index] = descriptor;
    }
#ifdef __APPLE__
    int enabled = 1;
    setsockopt(pair[0], SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(pair[0]); close(pair[1]); return std::unexpected("Could not initialize Python process actions");
    }
    const int setup = posix_spawn_file_actions_adddup2(&actions, pair[1], STDIN_FILENO) |
                      posix_spawn_file_actions_adddup2(&actions, pair[1], STDOUT_FILENO) |
                      posix_spawn_file_actions_addclose(&actions, pair[0]) |
                      posix_spawn_file_actions_addclose(&actions, pair[1]);
    std::string program = executable.string(), source{workerSource}, iterations = std::to_string(steps);
    char* arguments[]{program.data(), const_cast<char*>("-B"), const_cast<char*>("-u"),
                      const_cast<char*>("-c"), source.data(), const_cast<char*>(device.c_str()),
                      iterations.data(), nullptr};
    pid_t pid = -1;
    const auto status = setup == 0 ? posix_spawnp(&pid, program.c_str(), &actions, nullptr, arguments, environ) : setup;
    posix_spawn_file_actions_destroy(&actions);
    close(pair[1]);
    if (status != 0) { close(pair[0]); return std::unexpected("Could not start the configured Python executable"); }
    process_ = pid;
    socket_ = pair[0];
    return {};
#else
    return std::unexpected("This platform does not support local subprocess inference");
#endif
}

bool ChildProcess::running() const {
#ifdef _WIN32
    return process_ != nullptr;
#else
    return process_ > 0;
#endif
}

void ChildProcess::stop() {
#ifdef _WIN32
    if (input_) CloseHandle(input_);
    if (output_) CloseHandle(output_);
    input_ = output_ = nullptr;
    if (process_) {
        if (WaitForSingleObject(process_, 1000) == WAIT_TIMEOUT) TerminateProcess(process_, 1);
        WaitForSingleObject(process_, 1000);
        CloseHandle(process_);
        process_ = nullptr;
    }
#elif defined(__unix__) || defined(__APPLE__)
    if (socket_ >= 0) { shutdown(socket_, SHUT_RDWR); close(socket_); socket_ = -1; }
    if (process_ > 0) {
        int status;
        pid_t waited;
        do { waited = waitpid(process_, &status, WNOHANG); } while (waited < 0 && errno == EINTR);
        if (waited == 0) {
            for (int attempt = 0; attempt < 100 && waited == 0; ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
                do { waited = waitpid(process_, &status, WNOHANG); } while (waited < 0 && errno == EINTR);
            }
            if (waited == 0) {
                kill(process_, SIGKILL);
                do { waited = waitpid(process_, &status, 0); } while (waited < 0 && errno == EINTR);
            }
        }
        process_ = -1;
    }
#endif
}

bool ChildProcess::transfer(std::uint8_t* data, std::size_t size, bool writing, Deadline deadline) {
    while (size > 0 && std::chrono::steady_clock::now() < deadline) {
#ifdef _WIN32
        DWORD count = 0;
        if (writing) {
            if (!WriteFile(input_, data, static_cast<DWORD>(std::min<std::size_t>(size, 16384)), &count, nullptr)) return false;
        } else {
            DWORD available = 0;
            if (!PeekNamedPipe(output_, nullptr, 0, nullptr, &available, nullptr)) return false;
            if (available == 0) { Sleep(2); continue; }
            if (!ReadFile(output_, data, static_cast<DWORD>(std::min<std::size_t>(size, available)), &count, nullptr)) return false;
        }
#elif defined(__unix__) || defined(__APPLE__)
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        pollfd descriptor{socket_, static_cast<short>(writing ? POLLOUT : POLLIN), 0};
        const auto status = poll(&descriptor, 1, static_cast<int>(std::clamp<std::int64_t>(remaining.count(), 1, 1000)));
        if (status < 0 && errno == EINTR) continue;
        if (status < 0) return false;
        if (status == 0) continue;
        if (!(descriptor.revents & descriptor.events)) return false;
        int flags = MSG_DONTWAIT;
#ifndef __APPLE__
        if (writing) flags |= MSG_NOSIGNAL;
#endif
        const auto count = writing ? send(socket_, data, std::min<std::size_t>(size, 16384), flags)
                                   : recv(socket_, data, size, flags);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
#else
        const std::ptrdiff_t count = 0;
        return false;
#endif
        if (count <= 0) return false;
        data += count;
        size -= count;
    }
    return size == 0;
}

std::expected<Bytes, std::string> ChildProcess::exchange(std::span<const std::uint8_t> request,
                                                       std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::array<std::uint8_t, 8> header{};
    for (int index = 0; index < 8; ++index) header[index] = (std::uint64_t{request.size()} >> (8 * index)) & 0xff;
    Bytes data(request.begin(), request.end());
    if (!transfer(header.data(), header.size(), true, deadline) ||
        !transfer(data.data(), data.size(), true, deadline) ||
        !transfer(header.data(), header.size(), false, deadline)) {
        stop(); return std::unexpected("Python runtime exited or exceeded its timeout");
    }
    std::uint64_t length = 0;
    for (int index = 0; index < 8; ++index) length |= std::uint64_t{header[index]} << (8 * index);
    if (length == 0 || length > 128 * 1024 * 1024) {
        stop(); return std::unexpected("Python runtime returned an invalid response length");
    }
    Bytes response(length);
    if (!transfer(response.data(), response.size(), false, deadline)) {
        stop(); return std::unexpected("Python runtime returned a truncated response or exceeded its timeout");
    }
    return response;
}

} // namespace iiAudioGen::detail

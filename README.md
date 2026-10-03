# iiAudioGen

Initial C++23 SDK scaffold for AI sound generation, version 0.1.0. It uses the C++ standard library without Qt or third-party runtime dependencies.

The module defines generation requests, interleaved floating-point PCM results, structured errors, and a replaceable backend interface. **No AI model or inference backend is implemented.** A valid request sent to the default generator returns `ErrorCode::BackendUnavailable`.

## Build and test

CMake 3.24 or later and a compiler with C++23 `std::expected` support are required. Use the repository-local `build/` directory.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Shared libraries are enabled by default. Set `-DBUILD_SHARED_LIBS=OFF` for a static library. Tests use a recording backend to verify request validation, request/result forwarding, backend lifetime, missing-backend behavior, and error propagation. They do not perform AI inference.

## Install and consume

The standalone default install prefix is `~/.local/SDK/iiAudioGen`. Set `CMAKE_INSTALL_PREFIX` during configuration to select another prefix. An installation inside `build/` allows package verification without updating the user's SDK installation:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/stage"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release
cmake -S tests/consumer -B build/consumer/build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/build/stage"
cmake --build build/consumer/build --config Release
ctest --test-dir build/consumer/build -C Release --output-on-failure
```

Consumers use the exported CMake target and namespaced header:

```cmake
find_package(iiAudioGen 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE iiAudioGen::iiAudioGen)
```

```cpp
#include <iiAudioGen/iiAudioGen.h>

iiAudioGen::Generator generator;
iiAudioGen::GenerationRequest request{"Soft rain on a window"};
auto outcome = generator.generate(request);
// The scaffold returns BackendUnavailable for this valid request.
```

Implement `iiAudioGen::Backend::generate()` and pass a `std::shared_ptr<Backend>` to `Generator` to connect a real runtime later. Backend-specific model loading, resource limits, output validity, and concurrency belong to that implementation.

## Scope and layout

- `src/iiAudioGen/`: public API and implementation, located together.
- `cmake/`: installed CMake package configuration.
- `tests/`: behavioral contracts and a separate installed-package consumer.
- `docs/Architecture.md`: API responsibilities and current boundaries, in Korean.

Model downloads, training, inference, audio playback, file encoding, device acceleration, and GUI integration are outside this initial scaffold. Only the host build and installed package are covered by the local verification; other platforms require their own builds.

cmake_minimum_required(VERSION 3.24)
project(iiAudioGenConsumer LANGUAGES CXX)

enable_testing()
find_package(iiAudioGen 0.1 CONFIG REQUIRED)
add_executable(iiAudioGenConsumer main.cpp)
target_link_libraries(iiAudioGenConsumer PRIVATE iiAudioGen::iiAudioGen)
set_target_properties(iiAudioGenConsumer PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME iiAudioGen.installed_consumer COMMAND iiAudioGenConsumer)

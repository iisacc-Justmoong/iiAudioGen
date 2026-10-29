# iisdk:consumer-application
cmake_minimum_required(VERSION 3.24)
project(iiAudioGenTools LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)
# Consumer applications compose both independent SDK packages explicitly.
find_package(iiAudioGen 0.3 CONFIG REQUIRED)
find_package(iiFileProviderNative 0.6 CONFIG REQUIRED)

add_executable(iiAudioGenGenerate generate.cpp)
target_compile_features(iiAudioGenGenerate PRIVATE cxx_std_23)
target_link_libraries(iiAudioGenGenerate PRIVATE iiAudioGen::iiAudioGen iiFileProvider::Native)
set_target_properties(iiAudioGenGenerate PROPERTIES OUTPUT_NAME iiaudio-generate
    CXX_EXTENSIONS OFF INSTALL_RPATH_USE_LINK_PATH TRUE)
if(APPLE)
    set_target_properties(iiAudioGenGenerate PROPERTIES INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
elseif(UNIX)
    set_target_properties(iiAudioGenGenerate PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()
install(TARGETS iiAudioGenGenerate RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_TESTING)
    add_executable(iiAudioGenGeneratedFileTests ../tests/GeneratedFileTests.cpp)
    target_link_libraries(iiAudioGenGeneratedFileTests PRIVATE iiAudioGen::iiAudioGen iiFileProvider::Native)
    add_test(NAME iiAudioGen.GeneratedFile COMMAND iiAudioGenGeneratedFileTests)
    set_tests_properties(iiAudioGen.GeneratedFile PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../resources")
    add_executable(iiAudioGenRealInference ../tests/real_inference.cpp)
    target_link_libraries(iiAudioGenRealInference PRIVATE iiAudioGen::iiAudioGen iiFileProvider::Native)
    set(IIAUDIOGEN_TEST_MODEL "" CACHE PATH "Optional real local AudioLDM model")
    set(IIAUDIOGEN_TEST_PYTHON "python3" CACHE FILEPATH "Python containing the inference requirements")
    set(IIAUDIOGEN_TEST_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../resources" CACHE PATH
        "Directory for optional generated-audio verification outputs")
    if(IIAUDIOGEN_TEST_MODEL)
        add_test(NAME iiAudioGen.real_inference COMMAND iiAudioGenRealInference
            "${IIAUDIOGEN_TEST_MODEL}" "${IIAUDIOGEN_TEST_PYTHON}" "${IIAUDIOGEN_TEST_OUTPUT_DIR}")
        set_tests_properties(iiAudioGen.real_inference PROPERTIES TIMEOUT 600 LABELS "real-model")
    endif()
endif()

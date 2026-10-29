cmake_minimum_required(VERSION 3.24)
project(iiAudioGen VERSION 0.3.1 LANGUAGES CXX)






if(PROJECT_IS_TOP_LEVEL AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
        set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local/SDK/iiAudioGen" CACHE PATH "Install prefix" FORCE)
    elseif(DEFINED ENV{USERPROFILE} AND NOT "$ENV{USERPROFILE}" STREQUAL "")
        set(CMAKE_INSTALL_PREFIX "$ENV{USERPROFILE}/.local/SDK/iiAudioGen" CACHE PATH "Install prefix" FORCE)
    else()
        message(FATAL_ERROR "Set CMAKE_INSTALL_PREFIX explicitly")
    endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(GenerateExportHeader)
include(CTest)
option(BUILD_SHARED_LIBS "Build iiAudioGen as a shared library" ON)
find_package(Threads REQUIRED)

file(READ runtime/diffusers_worker.py _worker_source)
configure_file(src/iiAudioGen/WorkerSource.h.in include/iiAudioGen/WorkerSource.h @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS runtime/diffusers_worker.py)
add_library(iiAudioGen src/iiAudioGen/iiAudioGen.cpp src/iiAudioGen/Diffusers.cpp src/iiAudioGen/ChildProcess.cpp)
target_link_libraries(iiAudioGen PRIVATE Threads::Threads)
target_include_directories(iiAudioGen PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/include/iiAudioGen")
add_library(iiAudioGen::iiAudioGen ALIAS iiAudioGen)
generate_export_header(iiAudioGen
    BASE_NAME IIAUDIOGEN
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/iiAudioGen/Export.h")
target_compile_features(iiAudioGen PUBLIC cxx_std_23)
target_include_directories(iiAudioGen PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(iiAudioGen PUBLIC IIAUDIOGEN_STATIC_DEFINE)
endif()
set_target_properties(iiAudioGen PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})

set(_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/iiAudioGen")
install(TARGETS iiAudioGen EXPORT iiAudioGenTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES src/iiAudioGen/iiAudioGen.h src/iiAudioGen/Diffusers.h
    "${CMAKE_CURRENT_BINARY_DIR}/include/iiAudioGen/Export.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiAudioGen)
install(EXPORT iiAudioGenTargets NAMESPACE iiAudioGen:: DESTINATION "${_package_dir}")
configure_package_config_file(cmake/iiAudioGenConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/iiAudioGenConfig.cmake"
    INSTALL_DESTINATION "${_package_dir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/iiAudioGenConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiAudioGenConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/iiAudioGenConfigVersion.cmake"
    DESTINATION "${_package_dir}")
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/iiAudioGen)
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DATADIR}/iiAudioGen/docs)
install(FILES runtime/requirements.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/iiAudioGen/runtime)

if(BUILD_TESTING)
    add_test(NAME iiAudioGen.sdk_boundary COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/sdk_boundary.cmake")
    find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
    add_test(NAME iiAudioGen.worker COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_worker.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/diffusers_worker.py"
        "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
    set_tests_properties(iiAudioGen.worker PROPERTIES TIMEOUT 30)
    if(UNIX)
        foreach(_fixture ok error badpcm badlength crash timeout)
            file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixtures/${_fixture}")
        endforeach()
        configure_file(tests/runtime_peer.py.in "fixtures/python fixture" @ONLY)
        file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/fixtures/python fixture"
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
        add_executable(iiAudioGenDiffusersTests tests/diffusers.cpp)
        target_link_libraries(iiAudioGenDiffusersTests PRIVATE iiAudioGen::iiAudioGen)
        add_test(NAME iiAudioGen.diffusers COMMAND iiAudioGenDiffusersTests
            "${CMAKE_CURRENT_BINARY_DIR}/fixtures/python fixture" "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
        set_tests_properties(iiAudioGen.diffusers PROPERTIES TIMEOUT 30)
    endif()
    add_executable(iiAudioGenTests tests/generation.cpp)
    target_link_libraries(iiAudioGenTests PRIVATE iiAudioGen::iiAudioGen)
    target_compile_definitions(iiAudioGenTests PRIVATE
        "IIAUDIOGEN_TEST_FIXTURES=\"${CMAKE_CURRENT_SOURCE_DIR}/resources/model-contract\"")
    set_target_properties(iiAudioGenTests PROPERTIES CXX_EXTENSIONS OFF)
    foreach(_case request unavailable delegation error model)
        add_test(NAME iiAudioGen.${_case} COMMAND iiAudioGenTests ${_case})
        set_tests_properties(iiAudioGen.${_case} PROPERTIES TIMEOUT 10)
    endforeach()
endif()

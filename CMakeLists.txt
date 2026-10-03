cmake_minimum_required(VERSION 3.24)
project(iiAudioGen VERSION 0.1.0 LANGUAGES CXX)

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

add_library(iiAudioGen src/iiAudioGen/iiAudioGen.cpp)
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
    SOVERSION ${PROJECT_VERSION_MAJOR})

set(_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/iiAudioGen")
install(TARGETS iiAudioGen EXPORT iiAudioGenTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES src/iiAudioGen/iiAudioGen.h
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

if(BUILD_TESTING)
    add_executable(iiAudioGenTests tests/generation.cpp)
    target_link_libraries(iiAudioGenTests PRIVATE iiAudioGen::iiAudioGen)
    set_target_properties(iiAudioGenTests PROPERTIES CXX_EXTENSIONS OFF)
    foreach(_case request unavailable delegation error)
        add_test(NAME iiAudioGen.${_case} COMMAND iiAudioGenTests ${_case})
        set_tests_properties(iiAudioGen.${_case} PROPERTIES TIMEOUT 10)
    endforeach()
endif()

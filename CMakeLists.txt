cmake_minimum_required(VERSION 3.12)

project(BlipSyncUGens
    VERSION 1.0.0
    DESCRIPTION "Band-limited harmonic-band oscillator with phase I/O and sync"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SUPERNOVA "Build plugins for supernova" OFF)
option(NATIVE "Optimise for the build machine's CPU" ON)
set(SC_PATH "" CACHE STRING "Path to SuperCollider source")

if(SC_PATH)
    set(SC_SOURCE_DIR ${SC_PATH})
elseif(DEFINED ENV{SC_PATH})
    set(SC_SOURCE_DIR $ENV{SC_PATH})
elseif(EXISTS "${CMAKE_SOURCE_DIR}/../supercollider")
    set(SC_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../supercollider")
elseif(EXISTS "/datadisk/src-external/supercollider")
    set(SC_SOURCE_DIR "/datadisk/src-external/supercollider")
elseif(EXISTS "$ENV{HOME}/src/supercollider")
    set(SC_SOURCE_DIR "$ENV{HOME}/src/supercollider")
else()
    message(FATAL_ERROR "SuperCollider source not found. Set SC_PATH:\n"
                        "  cmake -DSC_PATH=/path/to/supercollider ..")
endif()

message(STATUS "SuperCollider source: ${SC_SOURCE_DIR}")
if(NOT EXISTS "${SC_SOURCE_DIR}/include/plugin_interface/SC_PlugIn.h")
    message(FATAL_ERROR "SC_PlugIn.h not found in ${SC_SOURCE_DIR}/include/plugin_interface")
endif()

include_directories(
    ${SC_SOURCE_DIR}/include/plugin_interface
    ${SC_SOURCE_DIR}/include/common
    ${SC_SOURCE_DIR}/common
)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
    set(PLUGIN_EXTENSION ".scx")
    add_compile_definitions(SC_DARWIN)
    set(INSTALL_PATH "$ENV{HOME}/Library/Application Support/SuperCollider/Extensions/BlipSyncUGens")
elseif(WIN32)
    set(PLUGIN_EXTENSION ".scx")
    add_compile_definitions(SC_WIN32)
    set(INSTALL_PATH "$ENV{LOCALAPPDATA}/SuperCollider/Extensions/BlipSyncUGens")
else()
    set(PLUGIN_EXTENSION ".so")
    add_compile_definitions(SC_LINUX)
    add_compile_options(-fPIC)
    set(INSTALL_PATH "$ENV{HOME}/.local/share/SuperCollider/Extensions/BlipSyncUGens")
endif()

# NOTE: deliberately no -ffast-math. The kernel's accuracy rests on exact IEEE
# behaviour in the phase reduction and the sinc guards; reassociation there
# reintroduces the cancellation this UGen exists to avoid.
add_compile_options(-O3)
if(NATIVE AND NOT APPLE)
    add_compile_options(-march=native)
endif()

add_library(BlipSync MODULE plugins/BlipSync/BlipSync.cpp)
set_target_properties(BlipSync PROPERTIES PREFIX "" SUFFIX ${PLUGIN_EXTENSION})

add_library(PhaseLock MODULE plugins/PhaseLock/PhaseLock.cpp)
set_target_properties(PhaseLock PROPERTIES PREFIX "" SUFFIX ${PLUGIN_EXTENSION})

if(SUPERNOVA)
    add_library(BlipSync_supernova MODULE plugins/BlipSync/BlipSync.cpp)
    target_compile_definitions(BlipSync_supernova PRIVATE SUPERNOVA)
    set_target_properties(BlipSync_supernova PROPERTIES PREFIX "" SUFFIX ${PLUGIN_EXTENSION})
endif()

install(TARGETS BlipSync PhaseLock LIBRARY DESTINATION ${INSTALL_PATH})
install(FILES plugins/BlipSync/BlipSync.sc plugins/PhaseLock/PhaseLock.sc
        DESTINATION ${INSTALL_PATH}/Classes)
install(FILES plugins/BlipSync/BlipSync.schelp plugins/PhaseLock/PhaseLock.schelp
        DESTINATION ${INSTALL_PATH}/HelpSource/Classes)
if(SUPERNOVA)
    install(TARGETS BlipSync_supernova LIBRARY DESTINATION ${INSTALL_PATH})
endif()

message(STATUS "Install path: ${INSTALL_PATH}")

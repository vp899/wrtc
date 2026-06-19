# CMakeLists.txt for stream_client
#
# This is an alternative build file for building stream_client
# outside the WebRTC source tree.
#
# Usage:
#   mkdir build && cd build
#   cmake .. -DWEBRTC_ROOT=/path/to/webrtc/src
#   make -j$(nproc)

cmake_minimum_required(VERSION 3.10)
project(stream_client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# WebRTC root directory (must be set)
if(NOT DEFINED WEBRTC_ROOT)
    message(FATAL_ERROR "Please set WEBRTC_ROOT to the WebRTC source directory:
        cmake .. -DWEBRTC_ROOT=/path/to/webrtc/src")
endif()

# WebRTC build output directory
if(NOT DEFINED WEBRTC_BUILD)
    set(WEBRTC_BUILD "${WEBRTC_ROOT}/out/Release")
endif()

# Source files
set(SOURCES
    main.cc
    frame_generator.cc
    custom_video_source.cc
    stream_conductor.cc
)

# Header files
set(HEADERS
    frame_generator.h
    custom_video_source.h
    stream_conductor.h
)

# Add executable
add_executable(stream_client ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(stream_client PRIVATE
    ${WEBRTC_ROOT}
    ${WEBRTC_ROOT}/third_party/abseil-cpp
    ${WEBRTC_ROOT}/third_party/jsoncpp/source/include
)

# Compiler flags
target_compile_definitions(stream_client PRIVATE
    WEBRTC_POSIX
    WEBRTC_LINUX
    WEBRTC_USE_H264
    HAVE_WEBRTC_VIDEO
)

target_compile_options(stream_client PRIVATE
    -Wall
    -Wno-unused-parameter
    -Wno-deprecated-declarations
)

# Link directories (WebRTC build output)
target_link_directories(stream_client PRIVATE
    ${WEBRTC_BUILD}
    ${WEBRTC_BUILD}/obj
)

# Link libraries
# Note: The exact library names depend on your WebRTC build configuration
target_link_libraries(stream_client PRIVATE
    webrtc
    pthread
    dl
    rt
    m
)

# Install
install(TARGETS stream_client DESTINATION bin)
install FILES receiver.html DESTINATION share/stream_client)

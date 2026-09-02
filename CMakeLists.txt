cmake_minimum_required(VERSION 3.16)
project(OpenNOWXenonPortableChecks LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(OPENNOW_HOST_TESTS "Build portable protocol/format tests" ON)
option(OPENNOW_HAVE_FFMPEG "Compile the CPU decoder against host FFmpeg" OFF)

# Host CMake is only a portable-code sanity harness. The actual console target
# is Makefile.xenon because LibXenon's SDK provides its own rules/toolchain.
add_library(opennow_portable STATIC
    source/core/input_protocol.cpp
    source/core/rtp_h264.cpp
    source/media/yuv_convert.cpp
)
target_include_directories(opennow_portable PUBLIC include)
target_compile_options(opennow_portable PRIVATE -Wall -Wextra -Wpedantic)

if(OPENNOW_HAVE_FFMPEG)
  find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h REQUIRED)
  find_library(AVCODEC_LIBRARY avcodec REQUIRED)
  find_library(AVUTIL_LIBRARY avutil REQUIRED)
  target_sources(opennow_portable PRIVATE source/media/ffmpeg_h264_decoder.cpp)
  target_include_directories(opennow_portable PRIVATE ${AVCODEC_INCLUDE_DIR})
  target_link_libraries(opennow_portable PRIVATE ${AVCODEC_LIBRARY} ${AVUTIL_LIBRARY})
  target_compile_definitions(opennow_portable PUBLIC OPENNOW_HAVE_FFMPEG=1)
endif()

if(OPENNOW_HOST_TESTS)
  enable_testing()
  foreach(test_name endian input rtp_h264 queue yuv)
    add_executable(test_${test_name} tests/test_${test_name}.cpp)
    target_link_libraries(test_${test_name} PRIVATE opennow_portable)
    target_compile_options(test_${test_name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME ${test_name} COMMAND test_${test_name})
  endforeach()
endif()

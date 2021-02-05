cmake_minimum_required(VERSION 2.8)

project(kcp C CXX)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
endif()

if (NOT MUDUO_BUILD_DIR)
  message(FATAL_ERROR "not found MUDUO_BUILD_DIR")
endif()

# https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html
# https://gcc.gnu.org/onlinedocs/gcc-7.5.0/gcc/Optimize-Options.html
set(CXX_FLAGS
 # -DVALGRIND
 -DHAVE_TCMALLOC
 -DCHECK_PTHREAD_RETURN_VALUE
 -D_FILE_OFFSET_BITS=64 # off64_t
 -Wall
 -Wextra
 -Werror
 -fexceptions
 # -Wthread-safety clang
 -Wconversion
 -Wno-unused-parameter
 -Wold-style-cast
 -Woverloaded-virtual
 -Wpointer-arith
 -Wshadow
 -Wwrite-strings
 -march=native
 # https://gcc.gnu.org/wiki/Visibility
 -fvisibility="hidden"
 -fwrapv # allow wrap around
 -fno-strict-aliasing
 # -MMD
 -std=c++14
 -rdynamic
)
if(CMAKE_BUILD_BITS EQUAL 32)
  list(APPEND CXX_FLAGS "-m32")
endif()
string(REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS}")

set(MUDUO_INCLUDE_DIR ${MUDUO_BUILD_DIR}/include/)
set(MUDUO_LIBRARY_DIR ${MUDUO_BUILD_DIR}/lib/)
set(CCB_INCLUDE_DIR ../)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -fno-inline -fno-omit-frame-pointer -g -ggdb3")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -finline-limit=1000 -DNDEBUG")

find_path(TCMALLOC_INCLUDE_DIR google/heap-profiler.h REQUIRED)
find_library(TCMALLOC_LIBRARY NAMES tcmalloc_and_profiler REQUIRED)

if(TCMALLOC_INCLUDE_DIR AND TCMALLOC_LIBRARY)
  message(STATUS "found tcmalloc")
else()
  message(STATUS "not found tcmalloc")
endif()

include_directories(
  ${MUDUO_INCLUDE_DIR}
  ${CCB_INCLUDE_DIR}
  ${PROJECT_SOURCE_DIR}
)

link_directories(
  ${MUDUO_LIBRARY_DIR}
)

set(kcp_SRCS
  ikcp.c
  udp_socket.cc
  kcp_packets.cc
  kcp_session.cc
  kcp_client.cc
  kcp_server.cc
)

add_library(kcp ${kcp_SRCS})
# target_link_libraries(kcp muduo_net muduo_base z pthread tcmalloc)
target_link_libraries(kcp muduo_net muduo_base z pthread)

add_subdirectory(examples)


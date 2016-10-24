cmake_minimum_required(VERSION 2.6)

project(tcprelay C CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CXX_FLAGS
 -g
 # -DVALGRIND
 # -DMUDUO_STD_STRING
 -DCHECK_PTHREAD_RETURN_VALUE
 -D_FILE_OFFSET_BITS=64
 -Wall
 -Wextra
 -Werror
 -Wconversion
 -Wno-unused-parameter
 -Wold-style-cast
 -Woverloaded-virtual
 -Wpointer-arith
 -Wshadow
 -Wwrite-strings
 -march=native
 # -MMD
 # -std=c++0x
 -rdynamic
 )
if(CMAKE_BUILD_BITS EQUAL 32)
  list(APPEND CXX_FLAGS "-m32")
endif()
string(REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS}")

set(muduo_INCLUDE_DIR /home/johnsun/software/muduo/)
set(muduo_LINK_DIR /home/johnsun/software/build/release/lib/)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_CXX_FLAGS_DEBUG "-O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -finline-limit=1000 -DNDEBUG")
#set(EXECUTABLE_OUTPUT_PATH bin)

find_package(Boost REQUIRED)
find_path(CARES_INCLUDE_DIR ares.h)
find_library(CARES_LIBRARY NAMES cares)
find_path(TCMALLOC_INCLUDE_DIR google/heap-profiler.h)
find_library(TCMALLOC_LIBRARY NAMES tcmalloc_and_profiler)

if(CARES_INCLUDE_DIR AND CARES_LIBRARY)
  message(STATUS "found cares")
endif()
if(TCMALLOC_INCLUDE_DIR AND TCMALLOC_LIBRARY)
  message(STATUS "found tcmalloc")
endif()

include_directories(
    ${muduo_INCLUDE_DIR}
    )

link_directories(
    ${muduo_LINK_DIR}
    )

add_library(muduo_cdns resolver.cc)
target_link_libraries(muduo_cdns muduo_net muduo_base z pthread)
target_link_libraries(muduo_cdns cares tcmalloc)

add_library(encryptor_openssl encryptor_openssl.cc)
target_link_libraries(encryptor_openssl crypto ssl dl tcmalloc)

add_executable(tcprelay tcprelay.cc)
target_link_libraries(tcprelay muduo_net muduo_base muduo_cdns z pthread)
target_link_libraries(tcprelay encryptor_openssl tcmalloc gflags)


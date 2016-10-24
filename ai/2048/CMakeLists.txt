cmake_minimum_required(VERSION 2.6)

project(muduo C CXX)

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
 -O2
 # -MMD
 # -std=c++0x
 -rdynamic
 )
if(CMAKE_BUILD_BITS EQUAL 32)
  list(APPEND CXX_FLAGS "-m32")
endif()
string(REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS}")

set(MUDUO_INCLUDE_DIR /home/johnsun/software/muduo-master/)
set(MUDUO_LINK_DIR /home/johnsun/software/build/release/lib/)
set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_CXX_FLAGS_DEBUG "-O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -finline-limit=1000 -DNDEBUG")
#set(EXECUTABLE_OUTPUT_PATH bin)

find_package(Boost REQUIRED)
find_package(Protobuf)
find_package(CURL)
find_package(ZLIB)
find_path(CARES_INCLUDE_DIR ares.h)
find_library(CARES_LIBRARY NAMES cares)
find_path(MHD_INCLUDE_DIR microhttpd.h)
find_library(MHD_LIBRARY NAMES microhttpd)
find_library(BOOSTTEST_LIBRARY NAMES boost_unit_test_framework)
find_library(BOOSTPO_LIBRARY NAMES boost_program_options)
find_path(TCMALLOC_INCLUDE_DIR google/heap-profiler.h)
find_library(TCMALLOC_LIBRARY NAMES tcmalloc_and_profiler)
find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h)
find_library(HIREDIS_LIBRARY NAMES hiredis)
find_path(GD_INCLUDE_DIR gd.h)
find_library(GD_LIBRARY NAMES gd)

if(CARES_INCLUDE_DIR AND CARES_LIBRARY)
  message(STATUS "found cares")
endif()
if(CURL_FOUND)
  message(STATUS "found curl")
endif()
if(PROTOBUF_FOUND)
  message(STATUS "found protobuf")
endif()
if(TCMALLOC_INCLUDE_DIR AND TCMALLOC_LIBRARY)
  message(STATUS "found tcmalloc")
endif()
if(ZLIB_FOUND)
  message(STATUS "found zlib")
endif()
if(HIREDIS_INCLUDE_DIR AND HIREDIS_LIBRARY)
  message(STATUS "found hiredis")
endif()
if(GD_INCLUDE_DIR AND GD_LIBRARY)
  message(STATUS "found gd")
endif()

#include_directories(${Boost_INCLUDE_DIRS})
include_directories(
    ${MUDUO_INCLUDE_DIR}
    )

#string(TOUPPER ${CMAKE_BUILD_TYPE} BUILD_TYPE)
#message(STATUS "CXX_FLAGS = " ${CMAKE_CXX_FLAGS} " " ${CMAKE_CXX_FLAGS_${BUILD_TYPE}})
link_directories(
    ${MUDUO_LINK_DIR}
    )

add_library(protobuf_codec codec.cc)
target_link_libraries(protobuf_codec protobuf muduo_base muduo_net z pthread)

add_custom_command(OUTPUT ai_2048.pb.cc ai_2048.pb.h
  COMMAND protoc
  ARGS --cpp_out . ${CMAKE_CURRENT_SOURCE_DIR}/ai_2048.proto -I${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ai_2048.proto
  VERBATIM )

set_source_files_properties(ai_2048.pb.cc PROPERTIES COMPILE_FLAGS "-Wno-conversion -Wno-shadow")

add_library(ai_2048_proto ai_2048.pb.cc)
target_link_libraries(ai_2048_proto protobuf pthread)

add_library(ai_2048 ai_2048.cc)

add_executable(ai_2048_server server.cc)
set_target_properties(ai_2048_server PROPERTIES COMPILE_FLAGS "-Wno-error=shadow")
target_link_libraries(ai_2048_server protobuf_codec ai_2048_proto ai_2048)

add_executable(ai_2048_client client.cc)
set_target_properties(ai_2048_client PROPERTIES COMPILE_FLAGS "-Wno-error=shadow")
target_link_libraries(ai_2048_client protobuf_codec ai_2048_proto ai_2048)

add_custom_target(protobuf_codec_all
                  DEPENDS
                  ai_2048_server
                  ai_2048_client)

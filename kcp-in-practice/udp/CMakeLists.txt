
set(udp_SRCS
  udp_client.cc
  udp_socket.cc
  udp_server.cc
  )

add_library(udp ${udp_SRCS})
target_link_libraries(udp)

#add_library(udp_cpp11 ${udp_SRCS})
#target_link_libraries(udp_cpp11 pthread rt)
#set_target_properties(udp_cpp11 PROPERTIES COMPILE_FLAGS "-std=c++0x")

#install(TARGETS udp DESTINATION lib)
#install(TARGETS udp_cpp11 DESTINATION lib)
#
#file(GLOB HEADERS "*.h")
#install(FILES ${HEADERS} DESTINATION include/ccb/common)
#
#if(NOT CMAKE_BUILD_NO_EXAMPLES)
#  add_subdirectory(tests)
#endif()

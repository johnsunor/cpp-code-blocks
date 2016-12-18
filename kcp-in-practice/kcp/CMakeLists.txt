
set(kcp_SRCS
  kcp_session.cc
  ikcp.c
  )

add_library(kcp ${kcp_SRCS})
target_link_libraries(kcp)

#add_library(kcp_cpp11 ${kcp_SRCS})
#target_link_libraries(kcp_cpp11 pthread rt)
#set_target_properties(kcp_cpp11 PROPERTIES COMPILE_FLAGS "-std=c++0x")

#install(TARGETS kcp DESTINATION lib)
#install(TARGETS kcp_cpp11 DESTINATION lib)
#
#file(GLOB HEADERS "*.h")
#install(FILES ${HEADERS} DESTINATION include/ccb/common)
#
#if(NOT CMAKE_BUILD_NO_EXAMPLES)
#  add_subdirectory(tests)
#endif()

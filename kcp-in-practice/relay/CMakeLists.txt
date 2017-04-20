
add_executable(kcp_relay_backend kcp_relay_backend.cc ${ccb_INCLUDE_DIR}/common/debug/stack_trace.cc)
target_link_libraries(kcp_relay_backend muduo_net muduo_base z pthread)
target_link_libraries(kcp_relay_backend udp kcp)
target_link_libraries(kcp_relay_backend tcmalloc gflags)

add_executable(kcp_relay_frontend kcp_relay_frontend.cc ${ccb_INCLUDE_DIR}/common/debug/stack_trace.cc)
target_link_libraries(kcp_relay_frontend muduo_net muduo_base z pthread)
target_link_libraries(kcp_relay_frontend udp kcp)
target_link_libraries(kcp_relay_frontend tcmalloc gflags)


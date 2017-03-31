
add_executable(rtt_server rtt_server.cc)
target_link_libraries(rtt_server muduo_net muduo_base z pthread)
target_link_libraries(rtt_server udp kcp)
target_link_libraries(rtt_server tcmalloc gflags)

add_executable(rtt_client rtt_client.cc)
target_link_libraries(rtt_client muduo_net muduo_base z pthread)
target_link_libraries(rtt_client udp kcp)
target_link_libraries(rtt_client tcmalloc gflags)


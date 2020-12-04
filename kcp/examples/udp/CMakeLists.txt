add_executable(udp_client client.cc)
target_link_libraries(udp_client kcp)

add_executable(udp_server server.cc)
target_link_libraries(udp_server kcp)


add_executable(udp_client client.cc)
target_link_libraries(udp_client kcp)

add_executable(udp_server server.cc)
target_link_libraries(udp_server kcp)

add_executable(mcast mcast.cc)
target_link_libraries(mcast kcp)


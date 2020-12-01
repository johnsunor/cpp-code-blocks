add_executable(diff_client client.cc)
target_link_libraries(diff_client kcp)

add_executable(diff_server server.cc)
target_link_libraries(diff_server kcp)


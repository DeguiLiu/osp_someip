add_executable(someip_server_demo server_demo.cpp)
target_link_libraries(someip_server_demo PRIVATE osp_someip)

add_executable(someip_client_demo client_demo.cpp)
target_link_libraries(someip_client_demo PRIVATE osp_someip)

add_executable(someip_loopback_bench loopback_bench.cpp)
target_link_libraries(someip_loopback_bench PRIVATE osp_someip)

add_executable(someip_fragmented_demo fragmented_demo.cpp)
target_link_libraries(someip_fragmented_demo PRIVATE osp_someip)

# Same sources with E2E Profile 1 enabled, used to check the profile over UDP.
add_executable(someip_e2e_server_demo server_demo.cpp)
target_link_libraries(someip_e2e_server_demo PRIVATE osp_someip)
target_compile_definitions(someip_e2e_server_demo PRIVATE SOMEIP_DEMO_E2E=1)

add_executable(someip_e2e_client_demo client_demo.cpp)
target_link_libraries(someip_e2e_client_demo PRIVATE osp_someip)
target_compile_definitions(someip_e2e_client_demo PRIVATE SOMEIP_DEMO_E2E=1)

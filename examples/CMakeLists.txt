add_executable(someip_server_demo server_demo.cpp)
target_link_libraries(someip_server_demo PRIVATE osp_someip)

add_executable(someip_client_demo client_demo.cpp)
target_link_libraries(someip_client_demo PRIVATE osp_someip)

add_executable(someip_loopback_bench loopback_bench.cpp)
target_link_libraries(someip_loopback_bench PRIVATE osp_someip)

add_executable(someip_fragmented_demo fragmented_demo.cpp)
target_link_libraries(someip_fragmented_demo PRIVATE osp_someip)

# Same sources with an E2E profile enabled, used to check each profile over UDP.
foreach(profile 1 2 4 5 7)
    add_executable(someip_e2e${profile}_server_demo server_demo.cpp)
    target_link_libraries(someip_e2e${profile}_server_demo PRIVATE osp_someip)
    target_compile_definitions(someip_e2e${profile}_server_demo PRIVATE SOMEIP_DEMO_E2E=${profile})
endforeach()

foreach(profile 1 2 4 5 7)
    add_executable(someip_e2e${profile}_client_demo client_demo.cpp)
    target_link_libraries(someip_e2e${profile}_client_demo PRIVATE osp_someip)
    target_compile_definitions(someip_e2e${profile}_client_demo PRIVATE SOMEIP_DEMO_E2E=${profile})
endforeach()

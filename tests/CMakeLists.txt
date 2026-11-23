add_executable(kv_backend_test kv_backend_test.cpp)

target_link_libraries(kv_backend_test PRIVATE
    locking_backend
    mvcc_backend
)

add_test(NAME kv_backend_test COMMAND kv_backend_test)

add_subdirectory(locking)

add_executable(test_threadpool test_threadpool.cpp)
target_include_directories(test_threadpool PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_threadpool PRIVATE thread_pool Threads::Threads)
add_test(NAME test_threadpool COMMAND test_threadpool)

add_executable(test_epoll test_epoll.cpp)
target_include_directories(test_epoll PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_epoll PRIVATE network_reactor)
add_test(NAME test_epoll COMMAND test_epoll)

add_executable(test_resp test_resp.cpp)
target_include_directories(test_resp PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_resp PRIVATE resp_protocol)
add_test(NAME test_resp COMMAND test_resp)

add_executable(test_router test_router.cpp)
target_include_directories(test_router PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_router PRIVATE command_router locking_backend Threads::Threads)
add_test(NAME test_router COMMAND test_router)

add_executable(test_tcp_integration test_tcp_integration.cpp)
target_include_directories(test_tcp_integration PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_tcp_integration PRIVATE tcp_server locking_backend Threads::Threads)
add_test(NAME test_tcp_integration COMMAND test_tcp_integration)

add_executable(kv_backend_test kv_backend_test.cpp)
target_link_libraries(kv_backend_test PRIVATE mini_redis_core)

add_test(NAME kv_backend_test COMMAND kv_backend_test)

add_subdirectory(locking)

add_library(locking_backend
  backends/locking_backend.cpp
)
target_link_libraries(locking_backend PUBLIC mini_redis_core Threads::Threads)

add_library(mvcc_backend
  backends/mvcc_backend.cpp
)
target_link_libraries(mvcc_backend PUBLIC mini_redis_core Threads::Threads)

add_library(thread_pool
  thread/cpu_affinity.cpp
  thread/thread_pool.cpp
)
target_include_directories(thread_pool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(thread_pool PUBLIC Threads::Threads)

add_library(network_reactor
  network/epoll_reactor.cpp
)
target_include_directories(network_reactor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(resp_protocol
  protocol/resp_parser.cpp
  protocol/resp_encoder.cpp
)
target_include_directories(resp_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(command_router
  router/command_router.cpp
)
target_include_directories(command_router PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(command_router PUBLIC mini_redis_core resp_protocol)

add_library(tcp_server
  network/tcp_server.cpp
)
target_include_directories(tcp_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tcp_server PUBLIC network_reactor resp_protocol command_router thread_pool)

add_executable(mini_redis_server
  main.cpp
)
target_include_directories(mini_redis_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mini_redis_server PRIVATE tcp_server locking_backend mvcc_backend Threads::Threads)

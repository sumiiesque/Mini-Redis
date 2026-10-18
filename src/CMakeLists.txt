add_library(locking_backend
  backends/locking_backend.cpp
)
target_link_libraries(locking_backend PUBLIC mini_redis_core Threads::Threads)

add_library(mvcc_backend
  backends/mvcc_backend.cpp
)
target_link_libraries(mvcc_backend PUBLIC mini_redis_core Threads::Threads)

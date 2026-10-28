add_library(locking_backend
  backends/locking_backend.cpp
)
target_link_libraries(locking_backend PUBLIC mini_redis_core Threads::Threads)

cmake_minimum_required(VERSION 3.16)
project(MiniRedis CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

add_library(mini_redis_core INTERFACE)
target_include_directories(mini_redis_core INTERFACE ${CMAKE_SOURCE_DIR}/include)

find_package(Threads REQUIRED)

enable_testing()
add_subdirectory(tests)

cmake_minimum_required(VERSION 3.28)

project(MatchingEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(benchmark REQUIRED)

add_executable(build-debug src/test.cpp)
target_include_directories(build-debug PRIVATE include)
target_link_libraries(build-debug PRIVATE benchmark::benchmark)

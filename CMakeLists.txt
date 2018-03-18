cmake_minimum_required(VERSION 3.9.0)
project(ErrorHandling)

set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -std=c++14 -O3")


add_executable(error_handling_perf error_handling_perf.cpp)
target_link_libraries(error_handling_perf "benchmark")



cmake_minimum_required(VERSION 3.22.1)
project(iobench)
add_library(iobench SHARED iobench.cpp)
find_library(log-lib log)
target_link_libraries(iobench ${log-lib})

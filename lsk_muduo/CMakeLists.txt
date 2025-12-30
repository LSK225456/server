cmake_minimum_required(VERSION 2.5)
project(lsk_muduo)

# cmake => makefile make
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++11 -fPIC")

aux_source_directory(. SRC_LIST)
add_library(lsk_muduo SHARED ${SRC_LIST})
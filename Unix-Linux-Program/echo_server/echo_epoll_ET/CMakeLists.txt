cmake_minimum_required(VERSION 3.10)
project(echo_epoll_ET)

set(CMAKE_CXX_STANDARD 11)
find_package(Threads REQUIRED)

add_executable(server server.cpp error_handling.cpp)
add_executable(client client.cpp error_handling.cpp)

add_executable(reactor_server reactor_server.cpp)
target_link_libraries(reactor_server PUBLIC Threads::Threads)
add_executable(proactor_server proactor_server.cpp)
target_link_libraries(proactor_server PUBLIC Threads::Threads)

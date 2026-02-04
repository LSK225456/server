cmake_minimum_required(VERSION 3.10)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加 Protobuf 子目录
add_subdirectory(proto)

# 后续迭代会添加 codec、server、client 等子目录
# add_subdirectory(codec)
# add_subdirectory(server)
# add_subdirectory(client)

message(STATUS "AGV Server module configured")

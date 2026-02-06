cmake_minimum_required(VERSION 3.10)

# ==================== AGV Server 总入口 ====================

message(STATUS "Configuring AGV Server...")

# 添加子模块
add_subdirectory(proto)
add_subdirectory(codec)
add_subdirectory(gateway)
add_subdirectory(test)

message(STATUS "AGV Server configured")

cmake_minimum_required(VERSION 3.10)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加 Protobuf 子目录
add_subdirectory(proto)

# 添加 codec 子目录
add_subdirectory(codec)

# 添加 gateway 子目录
add_subdirectory(gateway)

# 链接顺序：gateway -> codec -> proto -> muduo

# ==================== Gateway 子目录 ====================
add_subdirectory(gateway)

# ==================== Protobuf 编译 ====================
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})  # Protobuf 生成的头文件路径

# 编译 .proto 文件
set(PROTO_FILES
    proto/common.proto
    proto/message.proto
)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# ==================== AGV Server 库 ====================
set(AGV_SERVER_SRCS
    AgvSession.cc
    GatewayServer.cc
    ${PROTO_SRCS}  # Protobuf 生成的源文件
)

add_library(agv_server ${AGV_SERVER_SRCS})
target_link_libraries(agv_server
    lsk_net
    lsk_base
    ${PROTOBUF_LIBRARIES}
)

# ==================== Gateway 主程序 ====================
add_executable(gateway_main gateway_main.cc)
target_link_libraries(gateway_main
    agv_server
    lsk_net
    lsk_base
    ${PROTOBUF_LIBRARIES}
)

# 安装到 bin 目录
install(TARGETS gateway_main DESTINATION ${PROJECT_SOURCE_DIR}/bin)

message(STATUS "AGV Server module configured")

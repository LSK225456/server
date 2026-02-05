cmake_minimum_required(VERSION 3.10)

# 查找 Protobuf
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})  # 生成的 .pb.h 在此

# ==================== Protobuf 协议模块 ====================

message(STATUS "Configuring Proto module...")

# 设置 proto 文件列表
set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/common.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/message.proto
)

# 使用 protobuf_generate_cpp 生成 C++ 代码
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# 创建 Protobuf 静态库
add_library(agv_proto STATIC ${PROTO_SRCS} ${PROTO_HDRS})

# 链接 Protobuf 库
target_link_libraries(agv_proto ${PROTOBUF_LIBRARIES})

# 设置包含目录
target_include_directories(agv_proto PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
)

# 输出生成的文件信息
message(STATUS "Proto files: ${PROTO_FILES}")
message(STATUS "Generated sources: ${PROTO_SRCS}")
message(STATUS "Generated headers: ${PROTO_HDRS}")
message(STATUS "Proto module configured")

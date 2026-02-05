cmake_minimum_required(VERSION 3.10)
project(MuduoProtoTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖库
find_package(Protobuf REQUIRED)
find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

# 包含目录
include_directories(
    ${PROTOBUF_INCLUDE_DIRS}
    ${GTEST_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Proto 源文件
set(PROTO_SRCS
    ../agv_server/proto/common.pb.cc
    ../agv_server/proto/message.pb.cc
)

# Codec 源文件
set(CODEC_SRCS
    ../agv_server/codec/LengthHeaderCodec.cc
)

# ========== 链接 muduo 库（已预编译）==========
# 设置 muduo 库路径
set(MUDUO_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../lib)
link_directories(${MUDUO_LIB_DIR})

# ========== Proto 测试 ==========
add_executable(proto_test
    ProtoTest.cc
    ${PROTO_SRCS}
)

target_link_libraries(proto_test
    ${PROTOBUF_LIBRARIES}
    ${GTEST_BOTH_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)

# ========== Codec 测试 ==========
add_executable(codec_test
    CodecTest.cc
    ${PROTO_SRCS}
    ${CODEC_SRCS}
)

# 链接 muduo 库和其他依赖
target_link_libraries(codec_test
    lsk_muduo              # muduo 库
    ${PROTOBUF_LIBRARIES}
    ${GTEST_BOTH_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    rt                     # muduo 依赖的实时库
)

# 设置运行时库搜索路径
set_target_properties(codec_test PROPERTIES
    INSTALL_RPATH "${MUDUO_LIB_DIR}"
    BUILD_WITH_INSTALL_RPATH TRUE
)

# 启用测试
enable_testing()
add_test(NAME proto_test COMMAND proto_test)
add_test(NAME codec_test COMMAND codec_test)

# ==================== 单元测试 ====================
add_executable(gateway_test gateway_test.cc)
target_link_libraries(gateway_test
    agv_server
    lsk_net
    lsk_base
    ${PROTOBUF_LIBRARIES}
    ${GTEST_BOTH_LIBRARIES}
    pthread
)

# 添加到 CTest
add_test(NAME GatewayServerTest COMMAND gateway_test)

# 安装到 bin 目录
install(TARGETS gateway_test DESTINATION ${PROJECT_SOURCE_DIR}/bin)

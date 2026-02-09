cmake_minimum_required(VERSION 3.10)

# ==================== AGV Server 总入口 ====================

message(STATUS "Configuring AGV Server...")

# 添加子模块
add_subdirectory(proto)
add_subdirectory(codec)
add_subdirectory(gateway)

# ==================== Gateway Main 服务器主程序 ====================
add_executable(gateway_main
    GatewayMain.cc
)
target_link_libraries(gateway_main
    agv_gateway
    agv_codec
    agv_proto
    lsk_muduo_net
    lsk_muduo_base
    ${PROTOBUF_LIBRARIES}
    pthread
)
message(STATUS "Gateway Main configured")

message(STATUS "AGV Server configured")

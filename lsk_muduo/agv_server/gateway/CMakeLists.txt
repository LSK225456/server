# ==================== Gateway 模块 ====================

set(GATEWAY_SRCS
    AgvSession.cc
    GatewayServer.cc
)

add_library(agv_gateway STATIC ${GATEWAY_SRCS})

target_link_libraries(agv_gateway
    agv_codec
    agv_proto
    lsk_muduo
    ${PROTOBUF_LIBRARIES}
)

# ==================== Gateway 主程序 ====================

add_executable(gateway_main gateway_main.cc)

target_link_libraries(gateway_main
    agv_gateway
    agv_codec
    agv_proto
    lsk_muduo
    ${PROTOBUF_LIBRARIES}
    pthread
)

# 安装到 bin 目录
install(TARGETS gateway_main DESTINATION ${PROJECT_SOURCE_DIR}/../bin)

# ==================== Gateway 网关模块 ====================

message(STATUS "Configuring Gateway module...")

set(GATEWAY_SRCS
    AgvSession.cc
    SessionManager.cc
    GatewayServer.cc
    LatencyMonitor.cc
)

# 创建 Gateway 静态库
add_library(agv_gateway STATIC ${GATEWAY_SRCS})

target_include_directories(agv_gateway PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/agv_server/proto
)

target_link_libraries(agv_gateway
    agv_codec
    agv_proto
    lsk_muduo_net
    lsk_muduo_base
    ${PROTOBUF_LIBRARIES}
)



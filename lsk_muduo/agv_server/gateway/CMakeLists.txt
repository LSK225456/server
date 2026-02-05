# ==================== Gateway 模块 ====================

set(GATEWAY_SRCS
    AgvSession.cc
    GatewayServer.cc
)

add_library(agv_gateway STATIC ${GATEWAY_SRCS})

target_link_libraries(agv_gateway
    agv_codec
    agv_proto
    lsk_muduo_net
    lsk_muduo_base
    ${PROTOBUF_LIBRARIES}
)

target_include_directories(agv_gateway PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

# ==================== Gateway 主程序 ====================

add_executable(gateway_main gateway_main.cc)

target_link_libraries(gateway_main
    agv_gateway
    agv_codec
    agv_proto
    lsk_muduo_net
    lsk_muduo_base
    ${PROTOBUF_LIBRARIES}
    pthread
)

# 设置输出路径到 bin 目录
set_target_properties(gateway_main PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# 输出日志
message(STATUS "Gateway module configured (gateway_main)")

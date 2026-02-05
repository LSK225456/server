# ==================== Gateway 网关模块 ====================

message(STATUS "Configuring Gateway module...")

set(GATEWAY_SRCS
    AgvSession.cc
    GatewayServer.cc
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

# ==================== Gateway 主程序 ====================
add_executable(gateway_main gateway_main.cc)

target_include_directories(gateway_main PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/agv_server/proto
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

# 设置输出路径
set_target_properties(gateway_main PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

message(STATUS "Gateway module configured (gateway_main)")

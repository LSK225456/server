# ==================== Test 测试模块 ====================

message(STATUS "Configuring AGV Test module...")

# 重命名为 agv_gateway_test，避免与 test_muduo 冲突
add_executable(agv_gateway_test GatewayTest.cc)

target_include_directories(agv_gateway_test PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/agv_server/proto
)

target_link_libraries(agv_gateway_test
    agv_gateway
    agv_codec
    agv_proto
    lsk_muduo_net
    lsk_muduo_base
    ${PROTOBUF_LIBRARIES}
    pthread
)

set_target_properties(agv_gateway_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

message(STATUS "AGV Test module configured (agv_gateway_test)")

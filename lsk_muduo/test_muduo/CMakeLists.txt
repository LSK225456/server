

# ==================== 测试程序列表 ====================

# 以下测试需要 GTest，仅在 GTEST_FOUND 时构建
if(GTEST_FOUND)
    # 1. Buffer 测试
    add_executable(buffer_test BufferTest.cc)
    target_link_libraries(buffer_test 
        lsk_muduo_net 
        lsk_muduo_base
        ${GTEST_LIBRARIES} 
        pthread
    )

    # 2. Codec 测试  
    add_executable(codec_test CodecTest.cc)
    target_link_libraries(codec_test 
        agv_codec
        lsk_muduo_net 
        lsk_muduo_base
        ${GTEST_LIBRARIES}
        pthread
    )

    # 3. Protobuf 测试
    add_executable(proto_test ProtoTest.cc)
    target_link_libraries(proto_test
        agv_proto
        lsk_muduo_net
        lsk_muduo_base
        ${PROTOBUF_LIBRARIES}
        ${GTEST_LIBRARIES}
        pthread
    )
    
    message(STATUS "GTest-based tests configured: buffer_test, codec_test, proto_test")
else()
    message(STATUS "GTest not found, skipping GTest-based tests")
endif()

# 以下测试不需要 GTest，始终构建
# 4. Gateway 集成测试（包含新版 MockAgvClient）
add_executable(gateway_integration_test 
    gateway_test.cc 
    MockAgvClient.cc
)
target_link_libraries(gateway_integration_test
    agv_gateway
    agv_codec
    agv_proto
    lsk_muduo_net
    lsk_muduo_base
    ${PROTOBUF_LIBRARIES}
    pthread
)

# 5. MockAgvClient 功能测试（Day 3-4 验收测试）
add_executable(mock_agv_client_test
    MockAgvClientTest.cc
    MockAgvClient.cc
)
target_link_libraries(mock_agv_client_test
    agv_gateway
    agv_codec
    agv_proto
    lsk_muduo_net
    lsk_muduo_base
    ${PROTOBUF_LIBRARIES}
    pthread
)



# ==================== 输出目录配置 ====================

if(GTEST_FOUND)
    set_target_properties(
        buffer_test
        codec_test
        proto_test
        gateway_integration_test
        mock_agv_client_test
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    )
else()
    set_target_properties(
        gateway_integration_test
        mock_agv_client_test
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    )
endif()

# ==================== 总结信息 ====================

message(STATUS "Configuring Test module...")
if(GTEST_FOUND)
    message(STATUS "Test module configured (with GTest):")
    message(STATUS "  - buffer_test (GTest)")
    message(STATUS "  - codec_test (GTest)")
    message(STATUS "  - proto_test (GTest)")
    message(STATUS "  - gateway_integration_test")
    message(STATUS "  - mock_agv_client_test (Day 3-4)")
else()
    message(STATUS "Test module configured (without GTest):")
    message(STATUS "  - gateway_integration_test")
    message(STATUS "  - mock_agv_client_test (Day 3-4)")
endif()



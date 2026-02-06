

# ==================== 测试程序列表 ====================

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



# ==================== 输出目录配置 ====================

set_target_properties(
    buffer_test
    codec_test
    proto_test
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# ==================== 总结信息 ====================

message(STATUS "Configuring Test module...")
message(STATUS "Test module configured")
message(STATUS "  - buffer_test")
message(STATUS "  - codec_test")
message(STATUS "  - proto_test")

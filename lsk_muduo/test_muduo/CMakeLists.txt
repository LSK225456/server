# ==================== 测试程序列表 ====================

# 1. AGV Client Main — 独立 AGV 客户端工具（配合 gateway_main 手动测试）
add_executable(agv_client_main
    AgvClientMain.cc
    MockAgvClient.cc
)
target_link_libraries(agv_client_main
    agv_gateway
    agv_codec
    agv_proto
    lsk_muduo_net
    lsk_muduo_base
    ${PROTOBUF_LIBRARIES}
    pthread
)

# 2. 综合测试 — 覆盖迭代一到迭代三全部功能（需要 GTest）
if(GTEST_FOUND)
    add_executable(test_lsk_server
        test_lsk_server.cc
        MockAgvClient.cc
    )
    target_link_libraries(test_lsk_server
        agv_gateway
        agv_codec
        agv_proto
        lsk_muduo_net
        lsk_muduo_base
        ${PROTOBUF_LIBRARIES}
        ${GTEST_LIBRARIES}
        pthread
    )
endif()

# ==================== 输出目录配置 ====================

if(GTEST_FOUND)
    set_target_properties(
        agv_client_main
        test_lsk_server
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    )
else()
    set_target_properties(
        agv_client_main
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    )
endif()

# ==================== 总结信息 ====================

message(STATUS "Test module configured:")
message(STATUS "  - agv_client_main  [工具] 独立AGV客户端，配合 gateway_main 使用")
if(GTEST_FOUND)
    message(STATUS "  - test_lsk_server  [综合] 覆盖迭代一到迭代三全部功能")
else()
    message(STATUS "  - test_lsk_server  [跳过] GTest 未找到")
endif()

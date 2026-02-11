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

    # 4. ProtobufDispatcher 测试（迭代二：Day 1-2）
    add_executable(dispatcher_test DispatcherTest.cc)
    target_link_libraries(dispatcher_test
        agv_gateway
        agv_codec
        agv_proto
        lsk_muduo_net
        lsk_muduo_base
        ${PROTOBUF_LIBRARIES}
        ${GTEST_LIBRARIES}
        pthread
    )

    # 5. ConcurrentMap 测试（迭代二：Day 3-4）
    add_executable(concurrent_map_test ConcurrentMapTest.cc)
    target_link_libraries(concurrent_map_test
        lsk_muduo_net
        lsk_muduo_base
        ${GTEST_LIBRARIES}
        pthread
    )

    # 6. SessionManager 测试（迭代二：Day 5-7）
    add_executable(session_manager_test SessionManagerTest.cc)
    target_link_libraries(session_manager_test
        agv_gateway
        lsk_muduo_net
        lsk_muduo_base
        ${GTEST_LIBRARIES}
        pthread
    )

    message(STATUS "GTest-based tests configured: buffer_test, codec_test, proto_test, dispatcher_test, concurrent_map_test, session_manager_test")
else()
    message(STATUS "GTest not found, skipping GTest-based tests")
endif()

# 6. MockAgvClient 功能测试（Day 3-4 验收测试）
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

# 7. HeartbeatTest 心跳超时测试（迭代二：Day 3-4）
add_executable(heartbeat_test
    HeartbeatTest.cc
    MockAgvClient.cc
)
target_link_libraries(heartbeat_test
    agv_gateway
    agv_codec
    agv_proto
    lsk_muduo_net
    lsk_muduo_base
    ${PROTOBUF_LIBRARIES}
    pthread
)

# 8. AGV Client Main 独立可执行程序（迭代二：Day 1-2）
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

# 9. Quick Verify Test 快速验证测试（参数化配置验证）
add_executable(quick_verify_test
    QuickVerifyTest.cc
    MockAgvClient.cc
)
target_link_libraries(quick_verify_test
    agv_gateway
    agv_codec
    agv_proto
    lsk_muduo_net
    lsk_muduo_base
    ${PROTOBUF_LIBRARIES}
    pthread
)

# 10. Multi-Client Integration Test（迭代二：Day 5-7）
add_executable(multi_client_test
    MultiClientTest.cc
    MockAgvClient.cc
)
target_link_libraries(multi_client_test
    agv_gateway
    agv_codec
    agv_proto
    lsk_muduo_net
    lsk_muduo_base
    ${PROTOBUF_LIBRARIES}
    pthread
)

# 11. Worker Task Test（迭代三：Day 3-4）
add_executable(worker_task_test
    WorkerTaskTest.cc
)
target_link_libraries(worker_task_test
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
        dispatcher_test
        concurrent_map_test
        session_manager_test
        mock_agv_client_test
        heartbeat_test
        agv_client_main
        quick_verify_test
        gateway_main
        multi_client_test
        worker_task_test
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    )
else()
    set_target_properties(
        mock_agv_client_test
        heartbeat_test
        agv_client_main
        quick_verify_test
        gateway_main
        multi_client_test
        worker_task_test
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
    message(STATUS "  - dispatcher_test (GTest, 迭代二)")
    message(STATUS "  - concurrent_map_test (GTest, 迭代二)")
    message(STATUS "  - session_manager_test (GTest, 迭代二)")
    message(STATUS "  - mock_agv_client_test (迭代一)")
    message(STATUS "  - heartbeat_test (迭代二 Day 3-4)")
    message(STATUS "  - agv_client_main (迭代二 Day 1-2)")
    message(STATUS "  - worker_task_test (迭代三 Day 3-4)")
else()
    message(STATUS "Test module configured (without GTest):")
    message(STATUS "  - mock_agv_client_test (迭代一)")
    message(STATUS "  - heartbeat_test (迭代二 Day 3-4)")
    message(STATUS "  - agv_client_main (迭代二 Day 1-2)")
    message(STATUS "  - gateway_main (迭代二 Day 5-7)")
    message(STATUS "  - multi_client_test (迭代二 Day 5-7)")
    message(STATUS "  - worker_task_test (迭代三 Day 3-4)
    message(STATUS "  - gateway_main (迭代二 Day 5-7)")
    message(STATUS "  - multi_client_test (迭代二 Day 5-7)")
endif()


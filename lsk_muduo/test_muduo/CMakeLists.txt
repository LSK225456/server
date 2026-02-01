# test_muduo测试程序的CMakeLists.txt

# 设置头文件搜索路径
include_directories(${PROJECT_SOURCE_DIR})

# 编译测试服务器
add_executable(muduo_server muduo_server.cpp)

# 链接lsk_muduo库
target_link_libraries(muduo_server
    lsk_muduo
    pthread
    rt
)

# 设置输出目录
set_target_properties(muduo_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
)

message(STATUS "Test program muduo_server will be built")

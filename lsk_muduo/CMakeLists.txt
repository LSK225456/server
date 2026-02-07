cmake_minimum_required(VERSION 3.10)

project(lsk_muduo VERSION 1.0.0 LANGUAGES CXX C)

# ==================== 编译选项 ====================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译类型（默认 Release）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# ==================== 输出目录 ====================
set(LIBRARY_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/lib)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/bin)

# 确保目录存在
file(MAKE_DIRECTORY ${LIBRARY_OUTPUT_PATH})
file(MAKE_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})

# ==================== 打印配置信息 ====================
message(STATUS "========================================")
message(STATUS "Project: ${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Library Output: ${LIBRARY_OUTPUT_PATH}")
message(STATUS "Binary Output: ${EXECUTABLE_OUTPUT_PATH}")
message(STATUS "========================================")

# ==================== 查找依赖 ====================
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)

# GTest: 优先查找系统安装，找不到则自动下载编译
find_package(GTest QUIET)
if(NOT GTEST_FOUND)
    message(STATUS "System GTest not found, using FetchContent to download...")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        release-1.11.0
    )
    # 防止 GTest 覆盖父项目的编译选项
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    # 设置变量让后续 if(GTEST_FOUND) 为真
    set(GTEST_FOUND TRUE)
    set(GTEST_LIBRARIES gtest gtest_main)
    set(GTEST_INCLUDE_DIRS "${googletest_SOURCE_DIR}/googletest/include")
    include_directories(${GTEST_INCLUDE_DIRS})
endif()

message(STATUS "Protobuf version: ${Protobuf_VERSION}")
message(STATUS "Protobuf libraries: ${PROTOBUF_LIBRARIES}")
message(STATUS "Protobuf include dirs: ${PROTOBUF_INCLUDE_DIRS}")

# ==================== 包含目录 ====================
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${PROTOBUF_INCLUDE_DIRS})

# ==================== 构建 muduo 基础库 ====================

# muduo base 库源文件
set(MUDUO_BASE_SRCS
    muduo/base/AsyncLogging.cc
    muduo/base/CurrentThread.cc
    muduo/base/LogFile.cc
    muduo/base/Logger.cc
    muduo/base/LogStream.cc
    muduo/base/Thread.cc
    muduo/base/ThreadPool.cc
    muduo/base/Timestamp.cc
)

# muduo net 库源文件
set(MUDUO_NET_SRCS
    muduo/net/Acceptor.cc
    muduo/net/Buffer.cc
    muduo/net/Channel.cc
    muduo/net/Connector.cc
    muduo/net/DefaultPoller.cc
    muduo/net/EPollPoller.cc
    muduo/net/EventLoop.cc
    muduo/net/EventLoopThread.cc
    muduo/net/EventLoopThreadPool.cc
    muduo/net/InetAddress.cc
    muduo/net/Poller.cc
    muduo/net/Socket.cc
    muduo/net/TcpClient.cc
    muduo/net/TcpConnection.cc
    muduo/net/TcpServer.cc
    muduo/net/Timer.cc
    muduo/net/TimerQueue.cc
)

# 创建 muduo base 静态库
add_library(lsk_muduo_base STATIC ${MUDUO_BASE_SRCS})
target_include_directories(lsk_muduo_base PUBLIC ${CMAKE_SOURCE_DIR})

# 创建 muduo net 静态库
add_library(lsk_muduo_net STATIC ${MUDUO_NET_SRCS})
target_include_directories(lsk_muduo_net PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(lsk_muduo_net lsk_muduo_base pthread)

# ==================== 构建 AGV Server 模块 ====================
add_subdirectory(agv_server)

# ==================== 构建测试模块 ====================
# 注意：test_muduo 中的部分测试不需要 GTest，始终构建
enable_testing()
add_subdirectory(test_muduo)

# ==================== 安装规则 ====================
install(TARGETS lsk_muduo_base lsk_muduo_net
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY muduo/
    DESTINATION include/muduo
    FILES_MATCHING PATTERN "*.h"
)

message(STATUS "========================================")
message(STATUS "Configuration completed successfully!")
message(STATUS "========================================")
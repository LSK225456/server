# CMakeLists.txt for lsk_muduo
# 现代化、工业级别的CMake构建配置

cmake_minimum_required(VERSION 3.10)
project(lsk_muduo VERSION 1.0.0 LANGUAGES CXX C)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# 默认构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose Release or Debug" FORCE)
endif()

# 输出目录配置
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

# 设置位置无关代码（用于共享库）
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 头文件搜索路径
include_directories(${PROJECT_SOURCE_DIR})

# ==================== Base模块源文件 ====================
set(BASE_SRC
    muduo/base/Thread.cc
    muduo/base/ThreadPool.cc
    muduo/base/Timestamp.cc
    muduo/base/Logger.cc
    muduo/base/LogStream.cc
    muduo/base/LogFile.cc
    muduo/base/AsyncLogging.cc
    muduo/base/CurrentThread.cc
)

# ==================== Net模块源文件 ====================
set(NET_SRC
    muduo/net/EventLoop.cc
    muduo/net/EventLoopThread.cc
    muduo/net/EventLoopThreadPool.cc
    muduo/net/Channel.cc
    muduo/net/Poller.cc
    muduo/net/DefaultPoller.cc
    muduo/net/EPollPoller.cc
    muduo/net/Socket.cc
    muduo/net/Acceptor.cc
    muduo/net/InetAddress.cc
    muduo/net/TcpServer.cc
    muduo/net/TcpConnection.cc
    muduo/net/TcpClient.cc
    muduo/net/Connector.cc
    muduo/net/Buffer.cc
    muduo/net/Timer.cc
    muduo/net/TimerQueue.cc
)

# ==================== 合并所有源文件 ====================
set(MUDUO_SRC
    ${BASE_SRC}
    ${NET_SRC}
)

# ==================== 构建共享库 ====================
add_library(lsk_muduo SHARED ${MUDUO_SRC})

# 设置库的输出名称和版本
set_target_properties(lsk_muduo PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME "lsk_muduo"
    CLEAN_DIRECT_OUTPUT 1
)

# 链接系统库
target_link_libraries(lsk_muduo
    pthread
    rt        # 实时扩展库 (eventfd, timerfd等)
)

# ==================== 安装配置 ====================
# 安装共享库
install(TARGETS lsk_muduo
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# 安装头文件 - base模块
install(DIRECTORY muduo/base/
    DESTINATION include/muduo/base
    FILES_MATCHING PATTERN "*.h"
)

# 安装头文件 - net模块
install(DIRECTORY muduo/net/
    DESTINATION include/muduo/net
    FILES_MATCHING PATTERN "*.h"
)

# ==================== 打印配置信息 ====================
message(STATUS "========================================")
message(STATUS "Project: ${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Library Output: ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "========================================")

# ==================== 可选：编译测试程序 ====================
option(BUILD_TESTS "Build test programs" OFF)

if(BUILD_TESTS)
    add_subdirectory(test_muduo)
endif()

# 启用测试
enable_testing()

# 添加测试子目录
add_subdirectory(test_muduo)

# 添加 AGV 服务器模块
add_subdirectory(agv_server)

# ==================== 子目录：muduo 基础库 ====================
add_subdirectory(muduo/base)
add_subdirectory(muduo/net)

# ==================== 子目录：AGV 应用层 ====================
if(EXISTS "${CMAKE_SOURCE_DIR}/agv_server")
    add_subdirectory(agv_server)
endif()

# ==================== 子目录：测试 ====================
if(EXISTS "${CMAKE_SOURCE_DIR}/test_muduo" AND BUILD_TESTS)
    add_subdirectory(test_muduo)
endif()
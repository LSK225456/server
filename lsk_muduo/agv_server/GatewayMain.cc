#include "../agv_server/gateway/GatewayServer.h"
#include "../muduo/net/EventLoop.h"
#include "../muduo/net/InetAddress.h"
#include "../muduo/base/Logger.h"
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <signal.h>

/**
 * @brief GatewayServer 独立可执行程序（迭代二：Day 5-7）
 * 
 * @note 核心特性：
 *       1. 命令行参数化：支持 --port、--timeout、--threads
 *       2. 生产环境就绪：信号处理、优雅退出
 *       3. 多客户端联调：为 Day 5-7 验收测试提供服务端程序
 * 
 * @note 使用示例：
 *       # 基本用法（默认端口8000）
 *       ./gateway_main
 *       
 *       # 指定端口
 *       ./gateway_main --port 9000
 *       
 *       # 完整参数
 *       ./gateway_main --port 8000 --timeout 5.0 --threads 0
 * 
 * @note 参数说明：
 *       --port <port>      监听端口，默认 8000
 *       --timeout <sec>    会话超时时间（秒），默认 5.0
 *       --threads <num>    IO 线程数，0表示单Reactor，默认 0
 *       --help, -h         显示帮助信息
 */

using namespace lsk_muduo;
using namespace agv::gateway;

// ==================== 参数结构体 ====================

struct ServerConfig {
    uint16_t port = 8000;
    double session_timeout = 5.0;
    int num_threads = 0;
};

// ==================== 全局变量（信号处理）====================

static EventLoop* g_loop = nullptr;

void signalHandler(int signum) {
    LOG_INFO << "Received signal " << signum << ", shutting down...";
    if (g_loop) {
        g_loop->quit();
    }
}

// ==================== 参数解析函数 ====================

void printUsage(const char* program_name) {
    std::cout << "\nUsage: " << program_name << " [OPTIONS]\n"
              << "\nOptions:\n"
              << "  --port <port>      Listen port (default: 8000)\n"
              << "  --timeout <sec>    Session timeout in seconds (default: 5.0)\n"
              << "  --threads <num>    Number of IO threads, 0 for single-reactor (default: 0)\n"
              << "  --help, -h         Show this help message\n"
              << "\nExamples:\n"
              << "  " << program_name << "\n"
              << "  " << program_name << " --port 9000\n"
              << "  " << program_name << " --port 8000 --timeout 5.0 --threads 4\n"
              << std::endl;
}

bool parseArguments(int argc, char* argv[], ServerConfig& config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return false;
        }
        else if (arg == "--port") {
            if (i + 1 < argc) {
                config.port = static_cast<uint16_t>(std::stoi(argv[++i]));
                if (config.port == 0) {
                    std::cerr << "Error: --port must be in range [1, 65535]" << std::endl;
                    return false;
                }
            } else {
                std::cerr << "Error: --port requires an argument" << std::endl;
                return false;
            }
        }
        else if (arg == "--timeout") {
            if (i + 1 < argc) {
                config.session_timeout = std::stod(argv[++i]);
                if (config.session_timeout <= 0) {
                    std::cerr << "Error: --timeout must be positive" << std::endl;
                    return false;
                }
            } else {
                std::cerr << "Error: --timeout requires an argument" << std::endl;
                return false;
            }
        }
        else if (arg == "--threads") {
            if (i + 1 < argc) {
                config.num_threads = std::stoi(argv[++i]);
                if (config.num_threads < 0) {
                    std::cerr << "Error: --threads must be non-negative" << std::endl;
                    return false;
                }
            } else {
                std::cerr << "Error: --threads requires an argument" << std::endl;
                return false;
            }
        }
        else {
            std::cerr << "Error: Unknown option '" << arg << "'" << std::endl;
            printUsage(argv[0]);
            return false;
        }
    }
    
    return true;
}

// ==================== 主函数 ====================

int main(int argc, char* argv[]) {
    // 解析命令行参数
    ServerConfig config;
    if (!parseArguments(argc, argv, config)) {
        return 1;
    }
    
    // 打印配置信息
    std::cout << "\n========================================" << std::endl;
    std::cout << "GatewayServer Starting..." << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Listen Port:       " << config.port << std::endl;
    std::cout << "  Session Timeout:   " << config.session_timeout << " s" << std::endl;
    std::cout << "  IO Threads:        " << config.num_threads 
              << (config.num_threads == 0 ? " (Single-Reactor)" : "") << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    try {
        // 设置信号处理
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);
        
        // 创建事件循环
        EventLoop loop;
        g_loop = &loop;
        
        // 创建监听地址
        InetAddress listen_addr(config.port);
        
        // 创建 GatewayServer
        GatewayServer server(&loop, listen_addr, "GatewayServer", config.session_timeout);
        
        // 设置 IO 线程数（迭代三会使用，当前保持单Reactor）
        if (config.num_threads > 0) {
            server.setThreadNum(config.num_threads);
            LOG_INFO << "Multi-Reactor mode enabled with " << config.num_threads << " IO threads";
        } else {
            LOG_INFO << "Single-Reactor mode (no sub-reactors)";
        }
        
        // 启动服务器
        server.start();
        
        LOG_INFO << "GatewayServer started on port " << config.port;
        std::cout << "[INFO] GatewayServer is running... Press Ctrl+C to stop." << std::endl;
        
        // 启动事件循环
        loop.loop();
        
        LOG_INFO << "GatewayServer stopped";
        std::cout << "\n[INFO] GatewayServer gracefully shutdown." << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

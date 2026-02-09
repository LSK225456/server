#include "MockAgvClient.h"
#include "../muduo/net/EventLoop.h"
#include "../muduo/net/InetAddress.h"
#include "../muduo/base/Logger.h"
#include <iostream>
#include <cstring>
#include <cstdlib>

/**
 * @brief MockAgvClient 独立可执行程序（迭代二：Day 1-2）
 * 
 * @note 核心特性：
 *       1. 命令行参数化：支持 --id、--server、--freq、--battery、--timeout
 *       2. 压测复用：为迭代四的 LoadTester 提供基础组件
 *       3. 灵活配置：所有关键参数均可通过命令行指定
 * 
 * @note 使用示例：
 *       # 基本用法（所有参数使用默认值）
 *       ./agv_client_main
 *       
 *       # 指定车辆ID和服务器地址
 *       ./agv_client_main --id AGV-001 --server 127.0.0.1:8000
 *       
 *       # 低电量测试（初始电量15%，快速触发充电逻辑）
 *       ./agv_client_main --id AGV-002 --battery 15.0
 *       
 *       # 心跳超时测试（超时1秒，用于快速测试看门狗）
 *       ./agv_client_main --id AGV-003 --timeout 1.0
 *       
 *       # 完整参数示例
 *       ./agv_client_main --id AGV-004 --server 192.168.1.100:9000 \
 *                         --freq 100 --battery 80.0 --timeout 5.0
 * 
 * @note 参数说明：
 *       --id <string>      车辆ID，默认 "AGV-DEFAULT"
 *       --server <ip:port> 服务器地址，默认 "127.0.0.1:8000"
 *       --freq <Hz>        遥测发送频率，默认 50.0 Hz
 *       --battery <0-100>  初始电量，默认 100.0 %
 *       --timeout <sec>    看门狗超时，默认 5.0 秒
 */

// ==================== 参数结构体 ====================

struct ClientConfig {
    std::string agv_id = "AGV-DEFAULT";
    std::string server_ip = "127.0.0.1";
    uint16_t server_port = 8000;
    double telemetry_freq = 50.0;
    double initial_battery = 100.0;
    double watchdog_timeout = 5.0;
};

// ==================== 参数解析函数 ====================

void printUsage(const char* program_name) {
    std::cout << "\nUsage: " << program_name << " [OPTIONS]\n"
              << "\nOptions:\n"
              << "  --id <string>      AGV ID (default: AGV-DEFAULT)\n"
              << "  --server <ip:port> Server address (default: 127.0.0.1:8000)\n"
              << "  --freq <Hz>        Telemetry frequency (default: 50.0)\n"
              << "  --battery <0-100>  Initial battery level (default: 100.0)\n"
              << "  --timeout <sec>    Watchdog timeout (default: 5.0)\n"
              << "  --help, -h         Show this help message\n"
              << "\nExamples:\n"
              << "  " << program_name << "\n"
              << "  " << program_name << " --id AGV-001 --server 127.0.0.1:8000\n"
              << "  " << program_name << " --id AGV-002 --battery 15.0 --timeout 1.0\n"
              << std::endl;
}

bool parseArguments(int argc, char* argv[], ClientConfig& config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return false;
        }
        else if (arg == "--id") {
            if (i + 1 < argc) {
                config.agv_id = argv[++i];
            } else {
                std::cerr << "Error: --id requires an argument" << std::endl;
                return false;
            }
        }
        else if (arg == "--server") {
            if (i + 1 < argc) {
                std::string addr = argv[++i];
                size_t colon_pos = addr.find(':');
                if (colon_pos != std::string::npos) {
                    config.server_ip = addr.substr(0, colon_pos);
                    config.server_port = static_cast<uint16_t>(std::stoi(addr.substr(colon_pos + 1)));
                } else {
                    std::cerr << "Error: --server format should be <ip:port>" << std::endl;
                    return false;
                }
            } else {
                std::cerr << "Error: --server requires an argument" << std::endl;
                return false;
            }
        }
        else if (arg == "--freq") {
            if (i + 1 < argc) {
                config.telemetry_freq = std::stod(argv[++i]);
                if (config.telemetry_freq <= 0) {
                    std::cerr << "Error: --freq must be positive" << std::endl;
                    return false;
                }
            } else {
                std::cerr << "Error: --freq requires an argument" << std::endl;
                return false;
            }
        }
        else if (arg == "--battery") {
            if (i + 1 < argc) {
                config.initial_battery = std::stod(argv[++i]);
                if (config.initial_battery < 0.0 || config.initial_battery > 100.0) {
                    std::cerr << "Error: --battery must be in range [0.0, 100.0]" << std::endl;
                    return false;
                }
            } else {
                std::cerr << "Error: --battery requires an argument" << std::endl;
                return false;
            }
        }
        else if (arg == "--timeout") {
            if (i + 1 < argc) {
                config.watchdog_timeout = std::stod(argv[++i]);
                if (config.watchdog_timeout <= 0) {
                    std::cerr << "Error: --timeout must be positive" << std::endl;
                    return false;
                }
            } else {
                std::cerr << "Error: --timeout requires an argument" << std::endl;
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
    ClientConfig config;
    if (!parseArguments(argc, argv, config)) {
        return 1;
    }
    
    // 打印配置信息
    std::cout << "\n========================================" << std::endl;
    std::cout << "MockAgvClient Starting..." << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  AGV ID:            " << config.agv_id << std::endl;
    std::cout << "  Server:            " << config.server_ip << ":" << config.server_port << std::endl;
    std::cout << "  Telemetry Freq:    " << config.telemetry_freq << " Hz" << std::endl;
    std::cout << "  Initial Battery:   " << config.initial_battery << " %" << std::endl;
    std::cout << "  Watchdog Timeout:  " << config.watchdog_timeout << " s" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    try {
        // 创建事件循环
        lsk_muduo::EventLoop loop;
        
        // 创建服务器地址
        lsk_muduo::InetAddress server_addr(config.server_port, config.server_ip);
        
        // 创建 MockAgvClient
        MockAgvClient client(&loop,
                            server_addr,
                            config.agv_id,
                            config.telemetry_freq,
                            config.initial_battery,
                            config.watchdog_timeout);
        
        // 连接服务器
        client.connect();
        
        // 启动事件循环
        LOG_INFO << "MockAgvClient [" << config.agv_id << "] started";
        loop.loop();
        
        LOG_INFO << "MockAgvClient [" << config.agv_id << "] stopped";
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

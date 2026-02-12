#include <gtest/gtest.h>
#include "agv_server/gateway/SessionManager.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/TcpConnection.h"
#include "muduo/net/InetAddress.h"

#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <set>

using namespace agv::gateway;
using namespace lsk_muduo;

/**
 * @brief SessionManager 单元测试（迭代二：Day 5-7）
 * 
 * @note 覆盖场景：
 *       1. 基本会话注册和查找
 *       2. 会话移除
 *       3. 根据连接对象反查移除
 *       4. 批量操作（forEach、eraseIf）
 *       5. 统计查询（size、empty、getAllAgvIds）
 *       6. 并发安全性（多线程注册/查找/移除）
 *       7. AgvSession 持有连接弱引用的验证
 */

// ==================== 辅助函数 ====================

// 全局EventLoop用于测试（TcpConnection构造需要）
static EventLoop* g_loop = nullptr;

/**
 * @brief 创建模拟的 TcpConnection（仅用于测试）
 * @note 实际环境中，TcpConnection 由 TcpServer 创建
 */
std::shared_ptr<TcpConnection> createMockConnection(const std::string& name) {
    // 创建真实的 TcpConnection（需要 EventLoop 和有效的 socket fd）
    // 为了避免真实网络操作，我们使用假的 sockfd = -1
    return std::make_shared<TcpConnection>(
        g_loop,
        name,
        -1,  // 假的 sockfd（不会实际使用）
        InetAddress(8000, "127.0.0.1"),
        InetAddress(8001, "127.0.0.1")
    );
}

// ==================== 测试 1：基本注册和查找 ====================

TEST(SessionManagerTest, BasicRegisterAndFind) {
    SessionManager manager;
    
    EXPECT_TRUE(manager.empty());
    EXPECT_EQ(manager.size(), 0u);
    
    // 创建模拟连接
    auto conn = createMockConnection("TestConn");
    ASSERT_NE(conn, nullptr) << "创建连接失败";
    
    // 注册会话
    bool registered = manager.registerSession("AGV-001", conn);
    
    EXPECT_TRUE(registered) << "首次注册应返回 true";
    EXPECT_EQ(manager.size(), 1u);
    EXPECT_FALSE(manager.empty());
    EXPECT_TRUE(manager.hasSession("AGV-001"));
    
    // 查找会话
    auto session = manager.findSession("AGV-001");
    ASSERT_NE(session, nullptr) << "应能找到已注册的会话";
    EXPECT_EQ(session->getAgvId(), "AGV-001");
    EXPECT_DOUBLE_EQ(session->getBatteryLevel(), 100.0) << "默认满电";
    EXPECT_EQ(session->getState(), AgvSession::ONLINE) << "默认在线";
    
    // 查找不存在的会话
    auto notFound = manager.findSession("AGV-999");
    EXPECT_EQ(notFound, nullptr);
    EXPECT_FALSE(manager.hasSession("AGV-999"));
}

// ==================== 测试 2：重复注册（更新连接）====================

TEST(SessionManagerTest, RegisterDuplicate) {
    SessionManager manager;
    
    auto conn1 = createMockConnection("Conn1");
    auto conn2 = createMockConnection("Conn2");
    ASSERT_NE(conn1, nullptr);
    ASSERT_NE(conn2, nullptr);
    
    // 第一次注册
    bool result1 = manager.registerSession("AGV-001", conn1);
    EXPECT_TRUE(result1) << "首次注册应返回 true";
    
    // 同 ID 再次注册（模拟车辆重连）
    bool result2 = manager.registerSession("AGV-001", conn2);
    EXPECT_FALSE(result2) << "重复注册应返回 false";
    EXPECT_EQ(manager.size(), 1u) << "不应增加会话数量";
    
    // 验证连接已更新
    auto session = manager.findSession("AGV-001");
    ASSERT_NE(session, nullptr);
    auto sessionConn = session->getConnection();
    ASSERT_NE(sessionConn, nullptr);
    EXPECT_EQ(sessionConn.get(), conn2.get()) << "连接应更新为 conn2";
}

// ==================== 测试 3：移除会话 ====================

TEST(SessionManagerTest, RemoveSession) {
    SessionManager manager;
    
    auto conn = createMockConnection("Conn");
    ASSERT_NE(conn, nullptr);
    
    manager.registerSession("AGV-001", conn);
    manager.registerSession("AGV-002", conn);
    
    EXPECT_EQ(manager.size(), 2u);
    
    // 移除存在的会话
    bool removed1 = manager.removeSession("AGV-001");
    EXPECT_TRUE(removed1);
    EXPECT_EQ(manager.size(), 1u);
    EXPECT_FALSE(manager.hasSession("AGV-001"));
    EXPECT_TRUE(manager.hasSession("AGV-002"));
    
    // 移除不存在的会话
    bool removed2 = manager.removeSession("AGV-999");
    EXPECT_FALSE(removed2);
    EXPECT_EQ(manager.size(), 1u);
    
    // 清空所有会话
    manager.clear();
    EXPECT_EQ(manager.size(), 0u);
    EXPECT_TRUE(manager.empty());
}

// ==================== 测试 4：根据连接反查移除 ====================

TEST(SessionManagerTest, RemoveSessionByConnection) {
    SessionManager manager;
    
    auto conn1 = createMockConnection("Conn1");
    auto conn2 = createMockConnection("Conn2");
    ASSERT_NE(conn1, nullptr);
    ASSERT_NE(conn2, nullptr);
    
    manager.registerSession("AGV-001", conn1);
    manager.registerSession("AGV-002", conn1);  // 同一个连接
    manager.registerSession("AGV-003", conn2);  // 不同连接
    
    EXPECT_EQ(manager.size(), 3u);
    
    // 根据 conn1 移除
    size_t removed = manager.removeSessionByConnection(conn1);
    EXPECT_EQ(removed, 2u) << "应移除 AGV-001 和 AGV-002";
    EXPECT_EQ(manager.size(), 1u);
    EXPECT_FALSE(manager.hasSession("AGV-001"));
    EXPECT_FALSE(manager.hasSession("AGV-002"));
    EXPECT_TRUE(manager.hasSession("AGV-003"));
}

// ==================== 测试 5：forEach 遍历 ====================

TEST(SessionManagerTest, ForEach) {
    SessionManager manager;
    
    auto conn = createMockConnection("Conn");
    ASSERT_NE(conn, nullptr);
    
    manager.registerSession("AGV-001", conn);
    manager.registerSession("AGV-002", conn);
    manager.registerSession("AGV-003", conn);
    
    // 收集所有 agv_id
    std::set<std::string> ids;
    manager.forEach([&ids](const std::string& agv_id, const AgvSessionPtr& session) {
        ids.insert(agv_id);
        EXPECT_NE(session, nullptr);
    });
    
    EXPECT_EQ(ids.size(), 3u);
    EXPECT_TRUE(ids.count("AGV-001"));
    EXPECT_TRUE(ids.count("AGV-002"));
    EXPECT_TRUE(ids.count("AGV-003"));
}

// ==================== 测试 6：eraseIf 条件删除 ====================

TEST(SessionManagerTest, EraseIf) {
    SessionManager manager;
    
    auto conn = createMockConnection("Conn");
    ASSERT_NE(conn, nullptr);
    
    manager.registerSession("AGV-001", conn);
    manager.registerSession("AGV-002", conn);
    manager.registerSession("AGV-003", conn);
    
    // 修改某些会话的电量
    manager.findSession("AGV-001")->updateBatteryLevel(10.0);
    manager.findSession("AGV-002")->updateBatteryLevel(50.0);
    manager.findSession("AGV-003")->updateBatteryLevel(5.0);
    
    // 删除低电量（< 20%）的会话
    size_t removed = manager.eraseIf([](const std::string&, const AgvSessionPtr& session) {
        return session->getBatteryLevel() < 20.0;
    });
    
    EXPECT_EQ(removed, 2u) << "应删除 AGV-001 和 AGV-003";
    EXPECT_EQ(manager.size(), 1u);
    EXPECT_FALSE(manager.hasSession("AGV-001"));
    EXPECT_TRUE(manager.hasSession("AGV-002"));
    EXPECT_FALSE(manager.hasSession("AGV-003"));
}

// ==================== 测试 7：getAllAgvIds ====================

TEST(SessionManagerTest, GetAllAgvIds) {
    SessionManager manager;
    
    auto conn = createMockConnection("Conn");
    ASSERT_NE(conn, nullptr);
    
    manager.registerSession("AGV-003", conn);
    manager.registerSession("AGV-001", conn);
    manager.registerSession("AGV-002", conn);
    
    auto ids = manager.getAllAgvIds();
    EXPECT_EQ(ids.size(), 3u);
    
    // 转为 set 方便验证
    std::set<std::string> idSet(ids.begin(), ids.end());
    EXPECT_TRUE(idSet.count("AGV-001"));
    EXPECT_TRUE(idSet.count("AGV-002"));
    EXPECT_TRUE(idSet.count("AGV-003"));
}

// ==================== 测试 8：AgvSession 持有连接弱引用 ====================

TEST(SessionManagerTest, SessionHoldsWeakConnection) {
    SessionManager manager;
    
    auto conn = createMockConnection("Conn");
    ASSERT_NE(conn, nullptr);
    
    manager.registerSession("AGV-001", conn);
    
    auto session = manager.findSession("AGV-001");
    ASSERT_NE(session, nullptr);
    
    // 验证可以获取连接（在独立作用域内，确保临时shared_ptr销毁）
    {
        auto sessionConn = session->getConnection();
        ASSERT_NE(sessionConn, nullptr);
        EXPECT_EQ(sessionConn.get(), conn.get());
    }  // sessionConn 离开作用域，销毁
    
    // 释放原始连接（模拟连接断开）
    conn.reset();
    
    // 验证 weak_ptr 已失效
    auto disconnectedConn = session->getConnection();
    EXPECT_EQ(disconnectedConn, nullptr) << "连接断开后应返回 nullptr";
}

// ==================== 测试 9：并发注册和查找 ====================

TEST(SessionManagerTest, ConcurrentRegisterAndFind) {
    SessionManager manager;
    
    auto conn = createMockConnection("Conn");
    ASSERT_NE(conn, nullptr);
    
    const int kThreads = 10;
    const int kSessionsPerThread = 50;
    std::atomic<int> registerCount{0};
    std::atomic<int> findSuccessCount{0};
    
    std::vector<std::thread> threads;
    
    // 多个线程并发注册
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < kSessionsPerThread; ++i) {
                std::string agv_id = "AGV-" + std::to_string(t * kSessionsPerThread + i);
                manager.registerSession(agv_id, conn);
                registerCount++;
                
                // 立即查找验证
                auto session = manager.findSession(agv_id);
                if (session && session->getAgvId() == agv_id) {
                    findSuccessCount++;
                }
            }
        });
    }
    
    for (auto& th : threads) {
        th.join();
    }
    
    EXPECT_EQ(registerCount.load(), kThreads * kSessionsPerThread);
    EXPECT_EQ(findSuccessCount.load(), kThreads * kSessionsPerThread);
    EXPECT_EQ(manager.size(), static_cast<size_t>(kThreads * kSessionsPerThread));
}

// ==================== 测试 10：并发查找和移除 ====================

TEST(SessionManagerTest, ConcurrentFindAndRemove) {
    SessionManager manager;
    
    auto conn = createMockConnection("Conn");
    ASSERT_NE(conn, nullptr);
    
    // 预先注册 100 个会话
    const int kSessions = 100;
    for (int i = 0; i < kSessions; ++i) {
        manager.registerSession("AGV-" + std::to_string(i), conn);
    }
    
    EXPECT_EQ(manager.size(), static_cast<size_t>(kSessions));
    
    // 多线程并发操作
    std::atomic<int> findCount{0};
    std::atomic<int> removeCount{0};
    
    std::thread reader([&]() {
        for (int i = 0; i < kSessions; ++i) {
            if (manager.findSession("AGV-" + std::to_string(i))) {
                findCount++;
            }
        }
    });
    
    std::thread remover([&]() {
        for (int i = 0; i < kSessions / 2; ++i) {
            if (manager.removeSession("AGV-" + std::to_string(i))) {
                removeCount++;
            }
        }
    });
    
    reader.join();
    remover.join();
    
    EXPECT_EQ(removeCount.load(), kSessions / 2);
    EXPECT_EQ(manager.size(), static_cast<size_t>(kSessions / 2));
    
    // 验证剩余会话
    for (int i = kSessions / 2; i < kSessions; ++i) {
        EXPECT_TRUE(manager.hasSession("AGV-" + std::to_string(i)));
    }
}

// ==================== 主函数 ====================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // 创建全局 EventLoop（非 IO 线程，仅用于构造 TcpConnection）
    EventLoop loop;
    g_loop = &loop;
    
    return RUN_ALL_TESTS();
}

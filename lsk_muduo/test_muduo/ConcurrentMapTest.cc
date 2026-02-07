#include <gtest/gtest.h>
#include "agv_server/gateway/ConcurrentMap.h"

#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <string>
#include <set>

using namespace agv::gateway;

/**
 * @brief ConcurrentMap 单元测试（迭代二：Day 3-4）
 * 
 * @note 覆盖场景：
 *       1. 基本 CRUD 操作（insert / find / erase / clear）
 *       2. find 返回 shared_ptr 拷贝的生命周期安全性
 *       3. insertIfAbsent 条件插入
 *       4. forEach 遍历
 *       5. eraseIf 条件删除
 *       6. keys() 快照
 *       7. 并发读写安全性（多线程压测）
 *       8. 读写分离验证（多读者不阻塞）
 */

// ==================== 辅助测试类 ====================

struct TestSession {
    std::string id;
    double battery;
    int state;  // 0=ONLINE, 1=OFFLINE
    
    TestSession() : battery(100.0), state(0) {}
    explicit TestSession(const std::string& sid) 
        : id(sid), battery(100.0), state(0) {}
    TestSession(const std::string& sid, double bat)
        : id(sid), battery(bat), state(0) {}
};

// ==================== 测试 1：基本插入和查找 ====================

TEST(ConcurrentMapTest, BasicInsertAndFind) {
    ConcurrentMap<std::string, TestSession> map;
    
    EXPECT_TRUE(map.empty());
    EXPECT_EQ(map.size(), 0u);
    
    // 插入
    auto session = std::make_shared<TestSession>("AGV-001", 80.0);
    bool inserted = map.insert("AGV-001", session);
    
    EXPECT_TRUE(inserted) << "首次插入应返回 true";
    EXPECT_EQ(map.size(), 1u);
    EXPECT_FALSE(map.empty());
    
    // 查找
    auto found = map.find("AGV-001");
    ASSERT_NE(found, nullptr) << "应能找到已插入的元素";
    EXPECT_EQ(found->id, "AGV-001");
    EXPECT_DOUBLE_EQ(found->battery, 80.0);
    
    // 查找不存在的键
    auto notFound = map.find("AGV-999");
    EXPECT_EQ(notFound, nullptr) << "未插入的键应返回 nullptr";
}

// ==================== 测试 2：插入覆盖 ====================

TEST(ConcurrentMapTest, InsertOverwrite) {
    ConcurrentMap<std::string, TestSession> map;
    
    map.insert("AGV-001", std::make_shared<TestSession>("AGV-001", 80.0));
    
    // 同 key 再次插入，应覆盖
    bool inserted = map.insert("AGV-001", std::make_shared<TestSession>("AGV-001", 50.0));
    EXPECT_FALSE(inserted) << "覆盖已有 key 应返回 false";
    EXPECT_EQ(map.size(), 1u) << "覆盖不应增加 size";
    
    auto found = map.find("AGV-001");
    ASSERT_NE(found, nullptr);
    EXPECT_DOUBLE_EQ(found->battery, 50.0) << "应为覆盖后的值";
}

// ==================== 测试 3：insertIfAbsent ====================

TEST(ConcurrentMapTest, InsertIfAbsent) {
    ConcurrentMap<std::string, TestSession> map;
    
    // 第一次：key 不存在，应成功
    bool result1 = map.insertIfAbsent("AGV-001", std::make_shared<TestSession>("AGV-001", 80.0));
    EXPECT_TRUE(result1);
    
    // 第二次：key 已存在，应失败且不修改
    bool result2 = map.insertIfAbsent("AGV-001", std::make_shared<TestSession>("AGV-001", 50.0));
    EXPECT_FALSE(result2);
    
    auto found = map.find("AGV-001");
    ASSERT_NE(found, nullptr);
    EXPECT_DOUBLE_EQ(found->battery, 80.0) << "insertIfAbsent 不应覆盖已有值";
}

// ==================== 测试 4：删除 ====================

TEST(ConcurrentMapTest, Erase) {
    ConcurrentMap<std::string, TestSession> map;
    
    map.insert("AGV-001", std::make_shared<TestSession>("AGV-001"));
    map.insert("AGV-002", std::make_shared<TestSession>("AGV-002"));
    
    EXPECT_EQ(map.size(), 2u);
    
    bool erased = map.erase("AGV-001");
    EXPECT_TRUE(erased) << "删除已有 key 应返回 true";
    EXPECT_EQ(map.size(), 1u);
    EXPECT_EQ(map.find("AGV-001"), nullptr) << "删除后应找不到";
    EXPECT_NE(map.find("AGV-002"), nullptr) << "其他 key 不应受影响";
    
    // 删除不存在的 key
    bool erasedAgain = map.erase("AGV-001");
    EXPECT_FALSE(erasedAgain) << "删除不存在的 key 应返回 false";
}

// ==================== 测试 5：clear ====================

TEST(ConcurrentMapTest, Clear) {
    ConcurrentMap<std::string, TestSession> map;
    
    for (int i = 0; i < 100; i++) {
        map.insert("AGV-" + std::to_string(i), 
                   std::make_shared<TestSession>("AGV-" + std::to_string(i)));
    }
    
    EXPECT_EQ(map.size(), 100u);
    
    map.clear();
    
    EXPECT_EQ(map.size(), 0u);
    EXPECT_TRUE(map.empty());
    EXPECT_EQ(map.find("AGV-0"), nullptr);
}

// ==================== 测试 6：contains ====================

TEST(ConcurrentMapTest, Contains) {
    ConcurrentMap<std::string, TestSession> map;
    
    EXPECT_FALSE(map.contains("AGV-001"));
    
    map.insert("AGV-001", std::make_shared<TestSession>("AGV-001"));
    
    EXPECT_TRUE(map.contains("AGV-001"));
    EXPECT_FALSE(map.contains("AGV-002"));
}

// ==================== 测试 7：find 返回 shared_ptr 拷贝的生命周期安全性 ====================

TEST(ConcurrentMapTest, FindReturnsCopy_LifetimeSafety) {
    ConcurrentMap<std::string, TestSession> map;
    
    map.insert("AGV-001", std::make_shared<TestSession>("AGV-001", 90.0));
    
    // 获取 shared_ptr 拷贝
    auto sessionCopy = map.find("AGV-001");
    ASSERT_NE(sessionCopy, nullptr);
    
    // 从 map 中删除该 key
    map.erase("AGV-001");
    EXPECT_EQ(map.find("AGV-001"), nullptr) << "map 中应已删除";
    
    // 但之前持有的 shared_ptr 拷贝仍然有效（引用计数 >= 1）
    EXPECT_EQ(sessionCopy->id, "AGV-001") << "持有的拷贝仍应有效";
    EXPECT_DOUBLE_EQ(sessionCopy->battery, 90.0) << "持有的拷贝数据应完整";
}

// ==================== 测试 8：forEach 遍历 ====================

TEST(ConcurrentMapTest, ForEach) {
    ConcurrentMap<std::string, TestSession> map;
    
    map.insert("AGV-001", std::make_shared<TestSession>("AGV-001", 80.0));
    map.insert("AGV-002", std::make_shared<TestSession>("AGV-002", 60.0));
    map.insert("AGV-003", std::make_shared<TestSession>("AGV-003", 40.0));
    
    // 遍历统计
    int count = 0;
    double totalBattery = 0.0;
    std::set<std::string> visitedIds;
    
    map.forEach([&](const std::string& key, const std::shared_ptr<TestSession>& session) {
        count++;
        totalBattery += session->battery;
        visitedIds.insert(key);
    });
    
    EXPECT_EQ(count, 3);
    EXPECT_DOUBLE_EQ(totalBattery, 180.0);
    EXPECT_EQ(visitedIds.size(), 3u);
    EXPECT_TRUE(visitedIds.count("AGV-001"));
    EXPECT_TRUE(visitedIds.count("AGV-002"));
    EXPECT_TRUE(visitedIds.count("AGV-003"));
}

// ==================== 测试 9：eraseIf 条件删除 ====================

TEST(ConcurrentMapTest, EraseIf) {
    ConcurrentMap<std::string, TestSession> map;
    
    map.insert("AGV-001", std::make_shared<TestSession>("AGV-001", 80.0));
    map.insert("AGV-002", std::make_shared<TestSession>("AGV-002", 15.0));  // 低电量
    map.insert("AGV-003", std::make_shared<TestSession>("AGV-003", 10.0));  // 低电量
    map.insert("AGV-004", std::make_shared<TestSession>("AGV-004", 95.0));
    
    // 删除电量低于 20% 的会话
    size_t removed = map.eraseIf([](const std::string& /*key*/,
                                    const std::shared_ptr<TestSession>& session) {
        return session->battery < 20.0;
    });
    
    EXPECT_EQ(removed, 2u) << "应删除2个低电量会话";
    EXPECT_EQ(map.size(), 2u);
    EXPECT_NE(map.find("AGV-001"), nullptr);
    EXPECT_EQ(map.find("AGV-002"), nullptr) << "低电量应被删除";
    EXPECT_EQ(map.find("AGV-003"), nullptr) << "低电量应被删除";
    EXPECT_NE(map.find("AGV-004"), nullptr);
}

// ==================== 测试 10：keys() 快照 ====================

TEST(ConcurrentMapTest, Keys) {
    ConcurrentMap<std::string, TestSession> map;
    
    map.insert("B", std::make_shared<TestSession>("B"));
    map.insert("A", std::make_shared<TestSession>("A"));
    map.insert("C", std::make_shared<TestSession>("C"));
    
    auto keyList = map.keys();
    EXPECT_EQ(keyList.size(), 3u);
    
    // 排序后验证
    std::sort(keyList.begin(), keyList.end());
    EXPECT_EQ(keyList[0], "A");
    EXPECT_EQ(keyList[1], "B");
    EXPECT_EQ(keyList[2], "C");
}

// ==================== 测试 11：并发读写安全性 ====================

TEST(ConcurrentMapTest, ConcurrentReadWrite) {
    ConcurrentMap<std::string, TestSession> map;
    
    const int kNumWriters = 4;
    const int kNumReaders = 4;
    const int kOpsPerThread = 1000;
    
    std::atomic<int> writeSuccessCount(0);
    std::atomic<int> readSuccessCount(0);
    std::atomic<bool> startFlag(false);
    
    // 写线程：持续插入和删除
    auto writerFunc = [&](int threadId) {
        while (!startFlag.load()) { std::this_thread::yield(); }
        
        for (int i = 0; i < kOpsPerThread; i++) {
            std::string key = "AGV-" + std::to_string(threadId) + "-" + std::to_string(i);
            map.insert(key, std::make_shared<TestSession>(key, static_cast<double>(i)));
            writeSuccessCount++;
        }
        
        // 删除一半
        for (int i = 0; i < kOpsPerThread / 2; i++) {
            std::string key = "AGV-" + std::to_string(threadId) + "-" + std::to_string(i);
            map.erase(key);
        }
    };
    
    // 读线程：持续查找和遍历
    auto readerFunc = [&](int /*threadId*/) {
        while (!startFlag.load()) { std::this_thread::yield(); }
        
        for (int i = 0; i < kOpsPerThread; i++) {
            // 随机查找
            std::string key = "AGV-0-" + std::to_string(i % kOpsPerThread);
            auto result = map.find(key);
            if (result) {
                readSuccessCount++;
                // 验证 shared_ptr 指向的对象有效
                (void)result->id;
                (void)result->battery;
            }
            
            // 定期遍历
            if (i % 100 == 0) {
                int count = 0;
                map.forEach([&count](const std::string& /*k*/,
                                     const std::shared_ptr<TestSession>& /*v*/) {
                    count++;
                });
                (void)count;
            }
        }
    };
    
    // 启动线程
    std::vector<std::thread> threads;
    for (int i = 0; i < kNumWriters; i++) {
        threads.emplace_back(writerFunc, i);
    }
    for (int i = 0; i < kNumReaders; i++) {
        threads.emplace_back(readerFunc, i);
    }
    
    // 启动!
    startFlag.store(true);
    
    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    
    // 验证基本一致性
    EXPECT_EQ(writeSuccessCount.load(), kNumWriters * kOpsPerThread)
        << "所有写操作应完成";
    EXPECT_GT(readSuccessCount.load(), 0) << "应有读操作成功";
    
    // 每个写线程写了 kOpsPerThread 条，删了一半，剩 kOpsPerThread/2
    size_t expectedSize = kNumWriters * (kOpsPerThread / 2);
    EXPECT_EQ(map.size(), expectedSize) 
        << "最终 size 应为 " << expectedSize;
    
    SUCCEED() << "并发读写无崩溃、无死锁";
}

// ==================== 测试 12：并发多读者不阻塞 ====================

TEST(ConcurrentMapTest, ConcurrentReaders_NoBlocking) {
    ConcurrentMap<std::string, TestSession> map;
    
    // 预先插入数据
    for (int i = 0; i < 100; i++) {
        map.insert("AGV-" + std::to_string(i),
                   std::make_shared<TestSession>("AGV-" + std::to_string(i), static_cast<double>(i)));
    }
    
    const int kNumReaders = 8;
    const int kOpsPerReader = 5000;
    std::atomic<int> totalReadOps(0);
    std::atomic<bool> startFlag(false);
    
    auto readerFunc = [&]() {
        while (!startFlag.load()) { std::this_thread::yield(); }
        
        for (int i = 0; i < kOpsPerReader; i++) {
            auto result = map.find("AGV-" + std::to_string(i % 100));
            if (result) {
                totalReadOps++;
            }
            
            map.contains("AGV-" + std::to_string(i % 100));
            (void)map.size();
        }
    };
    
    std::vector<std::thread> threads;
    for (int i = 0; i < kNumReaders; i++) {
        threads.emplace_back(readerFunc);
    }
    
    auto startTime = std::chrono::steady_clock::now();
    startFlag.store(true);
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto endTime = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    EXPECT_EQ(totalReadOps.load(), kNumReaders * kOpsPerReader)
        << "所有读操作应完成";
    
    // 8 个读者各做 5000 次读，shared_lock 下应在合理时间内完成
    EXPECT_LT(elapsedMs, 5000) << "8个并发读者应在5秒内完成";
    
    SUCCEED() << "多读者并发无阻塞，耗时 " << elapsedMs << "ms";
}

// ==================== 主函数 ====================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

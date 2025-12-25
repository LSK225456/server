#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono> // 用于模拟耗时

// --- 共享资源区域 ---
std::queue<int> g_data_queue; // 1. 数据：就像桌子上的盘子
std::mutex g_mtx;             // 2. 锁：就像盘子旁边的“令牌”，谁拿到令牌谁才能动盘子
std::condition_variable g_cv; // 3. 电话：用于通知“盘子有东西了”

bool g_finished = false;      // 结束标志

// 辅助函数：打印带线程ID的日志
void print_log(const std::string& role, const std::string& msg) {
    // 为了防止打印错乱，打印本身也需要加锁（这里用cout自带的缓冲机制简化，实际项目中应单独加锁）
    std::cout << "[" << role << "] " << msg << std::endl;
}

// --- 消费者线程 (Consumer) ---
void consumer_thread() {
    while (true) {
        // Step 1: 抢锁
        print_log("Consumer", "1. 尝试获取互斥锁...");
        std::unique_lock<std::mutex> lk(g_mtx); 
        print_log("Consumer", "2. 拿到锁了！现在我是唯一能访问队列的人。");

        // Step 2: 检查条件 (核心循环)
        // 为什么用 while? 防止虚假唤醒。如果不满足条件，必须一直在这个循环里等。
        while (g_data_queue.empty() && !g_finished) {
            print_log("Consumer", "3. 发现队列是空的... 没办法处理。");
            print_log("Consumer", "4. === 关键动作 ===: 调用 wait()。我要释放锁并去睡觉了，等有人通知我。");
            
            // ★★★ 核心魔法发生在这里 ★★★
            // g_cv.wait(lk) 做原子操作：
            // 1. 自动 unlock(g_mtx) -> 让出令牌，这样生产者才能拿到锁往里放东西！此时消费者释放了 g_mtx，并阻塞在这一行。如果没有这一步，生产者永远拿不到锁，程序就死锁了。
            // 2. 线程进入阻塞状态 (Block) -> 不占用 CPU，彻底睡觉。
            // 3. ... (等待生产者 notify) ...
            // 4. 被唤醒后，自动 lock(g_mtx) -> 重新抢令牌，抢不到就接着等。
            g_cv.wait(lk); 

            print_log("Consumer", "5. 被唤醒了！我也自动重新拿到了锁。准备再次检查队列...");
        }

        // Step 3: 判断退出条件
        if (g_finished && g_data_queue.empty()) {
            print_log("Consumer", "收到结束信号且队列已空，下班。");
            break; // 此时 lk 会析构，自动解锁
        }

        // Step 4: 处理数据
        int data = g_data_queue.front();
        g_data_queue.pop();
        print_log("Consumer", "6. 取出数据: " + std::to_string(data) + "，当前队列剩余: " + std::to_string(g_data_queue.size()));

        // Step 5: 提前手动解锁
        // 处理数据通常很慢，不应该一直占着锁，否则生产者没法往里放新数据
        lk.unlock(); 
        print_log("Consumer", "7. 手动释放锁，开始模拟耗时的业务处理...");
        
        // 模拟处理数据的耗时 (1秒)
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        print_log("Consumer", "8. 数据 " + std::to_string(data) + " 处理完毕。\n");
        
    } // 循环回去，重新回到 Step 1 抢锁
}

// --- 生产者线程 (Producer) ---
void producer_thread() {
    for (int i = 1; i <= 3; ++i) {
        // 模拟生产数据的准备时间
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));

        print_log("Producer", "1. 准备生产数据，尝试获取锁...");
        
        { // 使用代码块控制锁的范围
            std::lock_guard<std::mutex> lk(g_mtx);
            print_log("Producer", "2. 拿到锁了！");
            
            g_data_queue.push(i);
            print_log("Producer", "3. 放入数据: " + std::to_string(i));
            
        } // 离开作用域，lock_guard 自动析构，解锁！
        print_log("Producer", "4. 释放锁完毕。");

        // ★★★ 通知 ★★★
        // 注意：建议在解锁后再通知，这叫 "Notify outside lock"，效率更高
        print_log("Producer", "5. 按铃通知 (notify_one)...");
        g_cv.notify_one(); 
    }

    // 结束流程
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_finished = true;
        print_log("Producer", "所有任务分发完毕，设置 finished = true");
    }
    g_cv.notify_all(); // 唤醒所有还在睡觉的消费者起来下班
}

int main() {
    print_log("Main", "程序启动...");
    
    // 先启动消费者，让他先去发现队列为空，从而演示“等待”的效果
    std::thread t_consumer(consumer_thread);
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 确保消费者先跑起来
    
    std::thread t_producer(producer_thread);

    t_producer.join();
    t_consumer.join();

    print_log("Main", "程序结束。");
    return 0;
}
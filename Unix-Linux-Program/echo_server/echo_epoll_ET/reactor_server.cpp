/**
 * Industrial-Style Multi-threaded Reactor Echo Server
 * * 对应《Linux高性能服务器编程》第八章 Reactor 模式详解
 * * 架构角色分工：
 * 1. Main Reactor (主线程):
 * - 唯一持有 epoll 实例。
 * - 职责：监听 listen_fd (新连接) 和 client_fd (数据到达)。
 * - 动作：accept 新连接，或者将活跃的 client_fd 封装成任务扔进线程池。
 * * 2. Worker Threads (工作线程池):
 * - 职责：处理具体的 I/O 读写 + 业务计算 (逻辑单元)。
 * - 动作：循环 read (ET模式) -> 业务处理(Echo) -> write -> 重置 EPOLLONESHOT。
 */

#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <cerrno>

// --- 配置参数 ---
const int PORT = 9190;
const int BUF_SIZE = 1024;
const int EPOLL_SIZE = 1000;
const int THREAD_POOL_SIZE = 4; // 根据 CPU 核心数调整

// --- 工具函数：设置非阻塞 ---
void setNonBlocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
}

// --- 工具函数：重置 EPOLLONESHOT ---
// 关键：工作线程处理完数据后，必须把 fd 放回 epoll 监听队列，
// 否则该 fd 后续的数据将无法触发 epoll_wait。
void resetOneShot(int epollfd, int fd) {
    epoll_event event;
    event.data.fd = fd;
    // 继续监听读事件，保持 ET 模式，保持 ONESHOT
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// ==========================================
// 1. 线程池类 (逻辑单元的容器)
// ==========================================
class ThreadPool {
public:
    ThreadPool(int num_threads, int epoll_fd) : stop(false), epoll_fd_(epoll_fd) {
        for (int i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    int sockfd;
                    {
                        // 1. 获取互斥锁
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        
                        // 2. 等待条件变量 (如果队列为空就阻塞)
                        this->condition.wait(lock, [this] { 
                            return this->stop || !this->tasks.empty(); 
                        });

                        // 3. 停止信号检查
                        if (this->stop && this->tasks.empty())
                            return;

                        // 4. 取出任务 (这里任务就是一个 socket fd)
                        sockfd = this->tasks.front();
                        this->tasks.pop();
                    } 
                    // 锁自动释放，开始并发执行任务

                    // 5. 执行具体的业务逻辑 (Worker Logic)
                    this->processTask(sockfd);
                }
            });
        }
    }

    // 主线程调用：添加任务到队列
    void enqueue(int sockfd) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.push(sockfd);
        }
        condition.notify_one(); // 唤醒一个昏睡的线程
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread &worker : workers)
            worker.join();
    }

private:
    // --- 核心业务逻辑 (这里是回声逻辑，但可以是任何复杂计算) ---
    void processTask(int sockfd) {
        char buf[BUF_SIZE];
        
        // [逻辑单元 - 读数据]
        // 由于是 ET 模式，必须循环读取直到 EAGAIN
        while (true) {
            memset(buf, 0, BUF_SIZE);
            ssize_t str_len = read(sockfd, buf, BUF_SIZE - 1);

            if (str_len == 0) {
                // 客户端断开连接
                close(sockfd);
                std::cout << "Closed client: " << sockfd << std::endl;
                return; // 结束，不再重置 ONESHOT
            } else if (str_len < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // 数据读完了，跳出循环
                    // [关键]：读完数据，重置 ONESHOT，让主线程能再次监听到该 socket
                    resetOneShot(epoll_fd_, sockfd);
                    break; 
                }
                // 真正的错误
                close(sockfd);
                return;
            } else {
                // [逻辑单元 - 业务计算 & 写数据]
                // 可以在这里插入复杂的导航算法、JSON解析等
                std::cout << "Worker thread " << std::this_thread::get_id() 
                          << " processing client " << sockfd << ": " << buf << std::endl;
                
                write(sockfd, buf, str_len); // Echo 回去
            }
        }
    }

    std::vector<std::thread> workers;
    std::queue<int> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
    int epoll_fd_; // 工作线程需要操作 epoll (重置 ONESHOT)
};

// ==========================================
// 2. Main Reactor (主程序 / I/O 处理单元)
// ==========================================
int main(int argc, char *argv[]) {
    int server_sock = socket(PF_INET, SOCK_STREAM, 0);
    
    // 端口复用 (防止 TIME_WAIT 导致 bind 失败)
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    if (bind(server_sock, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind() error");
        exit(1);
    }
    if (listen(server_sock, 5) == -1) {
        perror("listen() error");
        exit(1);
    }

    // 创建 epoll 实例
    int epfd = epoll_create(EPOLL_SIZE);
    epoll_event *ep_events = new epoll_event[EPOLL_SIZE];
    
    // 注册监听 socket (监听 socket 不需要 ONESHOT，因为只有一个主线程处理它)
    epoll_event event;
    event.events = EPOLLIN | EPOLLET; 
    event.data.fd = server_sock;
    epoll_ctl(epfd, EPOLL_CTL_ADD, server_sock, &event);

    // 启动线程池 (逻辑单元就位)
    ThreadPool pool(THREAD_POOL_SIZE, epfd);

    std::cout << "Reactor Server started on port " << PORT << "..." << std::endl;

    // --- 事件循环 (Event Loop) ---
    while (true) {
        // [I/O 单元 - 阻塞等待]
        int event_cnt = epoll_wait(epfd, ep_events, EPOLL_SIZE, -1);
        
        if (event_cnt == -1) {
            perror("epoll_wait() error");
            break;
        }

        for (int i = 0; i < event_cnt; ++i) {
            int sockfd = ep_events[i].data.fd;

            if (sockfd == server_sock) {
                // [I/O 单元 - 处理新连接]
                sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);
                int client_sock = accept(server_sock, (sockaddr*)&client_addr, &client_addr_len);
                
                setNonBlocking(client_sock);

                // [关键] 注册新连接到 epoll
                // 1. EPOLLIN: 读事件
                // 2. EPOLLET: 边缘触发 (高效)
                // 3. EPOLLONESHOT: 保证同一时刻只有一个线程处理该 socket
                event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                event.data.fd = client_sock;
                epoll_ctl(epfd, EPOLL_CTL_ADD, client_sock, &event);
                
                std::cout << "New connection: " << client_sock << std::endl;
            } 
            else {
                // [I/O 单元 - 仅分发任务]
                // 发生了读事件，但主线程如果不读，只负责把 fd 扔给线程池。
                // 具体的 read() 操作由 Worker 线程去完成。
                if (ep_events[i].events & EPOLLIN) {
                    pool.enqueue(sockfd);
                }
                // 注意：这里不需要手动移除 epoll 事件，因为 EPOLLONESHOT 会自动禁止
                // 直到 Worker 线程显式调用 resetOneShot
            }
        }
    }

    close(server_sock);
    close(epfd);
    delete[] ep_events;
    return 0;
}
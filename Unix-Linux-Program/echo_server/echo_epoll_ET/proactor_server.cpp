/**
 * Industrial-Style Simulated Proactor Server (Linux Epoll Implementation)
 * * --- 核心区别 (Vs Reactor) ---
 * [Reactor]:  主线程只管“有连接/有数据”，工作线程自己去 read() -> process() -> write()。
 * [Proactor]: 主线程负责“读数据”和“写数据”。工作线程只负责“纯粹的逻辑运算”。
 * 主线程像保姆一样，把饭(数据)喂到嘴边，工作线程只负责吃(处理)。
 * * --- 流程 (基于图 8-6 的 Linux 工程化落地) ---
 * 1. [Main]: epoll 监听到 EPOLLIN。
 * 2. [Main]: 循环 read() 直到 EAGAIN，将数据存入 client_context 的 read_buffer。
 * 3. [Main]: 将 client_context 指针扔入线程池。
 * 4. [Worker]: 从队列取出 context。
 * 5. [Worker]: 执行业务逻辑 (小写 -> 大写)，将结果写入 context 的 write_buffer。
 * 6. [Worker]: 修改 epoll 事件为 EPOLLOUT (请求主线程发货)。
 * 7. [Main]: epoll 监听到 EPOLLOUT。
 * 8. [Main]: 循环 write() 将 write_buffer 数据发出。
 */

#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <cerrno>

// --- 配置 ---
const int PORT = 9190;
const int BUF_SIZE = 4096; // 加大缓冲区，模拟真实场景
const int EPOLL_SIZE = 1000;
const int THREAD_POOL_SIZE = 4;

// --- 上下文结构体 (Client Context) ---
// Proactor 模式下，我们需要一个结构体来在主线程和工作线程之间传递数据
struct ClientContext {
    int sockfd;
    std::string read_buffer;  // 主线程读到的数据存这里
    std::string write_buffer; // 工作线程算好的数据存这里
    bool is_closed;           // 标记连接是否应关闭

    ClientContext() : sockfd(-1), is_closed(false) {}
    
    void clear() {
        read_buffer.clear();
        write_buffer.clear();
        is_closed = false;
    }
};

// 全局 map 管理 fd 到 context 的映射 (简化演示，生产环境可用更高效的数据结构)
ClientContext* g_contexts[65535]; 

void setNonBlocking(int fd) {
    int old = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, old | O_NONBLOCK);
}

// 修改 epoll 事件
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    // 依然使用 ET 模式 + ONESHOT，防止多线程竞争
    event.events = ev | EPOLLET | EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// ==========================================
// 1. 线程池 (只负责逻辑运算)
// ==========================================
class ThreadPool {
public:
    ThreadPool(int num_threads, int epoll_fd) : stop(false), epoll_fd_(epoll_fd) {
        for (int i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    ClientContext* ctx = nullptr;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] { 
                            return this->stop || !this->tasks.empty(); 
                        });
                        if (this->stop && this->tasks.empty()) return;
                        ctx = this->tasks.front();
                        this->tasks.pop();
                    }

                    // --- [Proactor 核心: 逻辑处理] ---
                    // 此时，数据已经在 ctx->read_buffer 里了！
                    // 工作线程完全不需要调用 read()，也不需要碰 socket。
                    this->processLogic(ctx);
                }
            });
        }
    }

    void enqueue(ClientContext* ctx) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.push(ctx);
        }
        condition.notify_one();
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (auto &worker : workers) worker.join();
    }

private:
    // 纯粹的 CPU 计算逻辑：小写转大写
    void processLogic(ClientContext* ctx) {
        // 模拟业务处理
        std::string &in = ctx->read_buffer;
        std::string &out = ctx->write_buffer;

        // 简单的回声逻辑：转大写
        out = in;
        std::transform(out.begin(), out.end(), out.begin(), ::toupper);

        std::cout << "[Worker Thread] Processed logic for FD " << ctx->sockfd 
                  << ". Input: " << in.size() << " bytes." << std::endl;

        // 清空读缓冲，为下一次做准备
        in.clear(); 

        // --- 关键步骤 ---
        // 逻辑处理完毕，数据已经在 write_buffer 里了。
        // 我们不直接 write，而是告诉主线程："老板，货备好了，你可以发货了(EPOLLOUT)。"
        modfd(epoll_fd_, ctx->sockfd, EPOLLOUT);
    }

    std::vector<std::thread> workers;
    std::queue<ClientContext*> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
    int epoll_fd_;
};

// ==========================================
// 2. 主线程 (I/O 处理单元)
// ==========================================
int main() {
    int server_sock = socket(PF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    if (bind(server_sock, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind"); exit(1);
    }
    if (listen(server_sock, 5) == -1) {
        perror("listen"); exit(1);
    }

    int epfd = epoll_create(EPOLL_SIZE);
    epoll_event *ep_events = new epoll_event[EPOLL_SIZE];
    
    epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = server_sock;
    epoll_ctl(epfd, EPOLL_CTL_ADD, server_sock, &event);

    ThreadPool pool(THREAD_POOL_SIZE, epfd);

    // 初始化 Context 数组
    for(int i=0; i<65535; ++i) g_contexts[i] = new ClientContext();

    std::cout << "Simulated Proactor Server running on port " << PORT << "..." << std::endl;

    while (true) {
        int event_cnt = epoll_wait(epfd, ep_events, EPOLL_SIZE, -1);
        if (event_cnt == -1) break;

        for (int i = 0; i < event_cnt; ++i) {
            int sockfd = ep_events[i].data.fd;

            // 1. 处理新连接
            if (sockfd == server_sock) {
                sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);
                int client_sock = accept(server_sock, (sockaddr*)&client_addr, &client_addr_len);
                setNonBlocking(client_sock);

                // 初始化该连接的 Context
                g_contexts[client_sock]->sockfd = client_sock;
                g_contexts[client_sock]->clear();

                // 注册 EPOLLIN | EPOLLONESHOT
                event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                event.data.fd = client_sock;
                epoll_ctl(epfd, EPOLL_CTL_ADD, client_sock, &event);
                std::cout << "New Client: " << client_sock << std::endl;
            } 
            // 2. 处理读事件 (主线程负责读!)
            else if (ep_events[i].events & EPOLLIN) {
                ClientContext* ctx = g_contexts[sockfd];
                char buf[BUF_SIZE];
                bool read_error = false;
                
                // [I/O 动作]: 主线程把数据一次性全部读完
                while (true) {
                    ssize_t len = read(sockfd, buf, BUF_SIZE);
                    if (len < 0) {
                        if (errno == EAGAIN) break; // 读完了
                        read_error = true; break;   // 出错了
                    } else if (len == 0) {
                        read_error = true; break;   // 对方关闭
                    } else {
                        ctx->read_buffer.append(buf, len);
                    }
                }

                if (read_error) {
                    close(sockfd);
                    ctx->clear();
                    printf("Client %d closed/error.\n", sockfd);
                } else {
                    // [分发动作]: 数据读好了，打包扔给 Worker
                    if (!ctx->read_buffer.empty()) {
                        pool.enqueue(ctx);
                    } else {
                        // 读了个寂寞(可能是空唤醒)，重新监听读
                        modfd(epfd, sockfd, EPOLLIN);
                    }
                }
            } 
            // 3. 处理写事件 (主线程负责写!)
            else if (ep_events[i].events & EPOLLOUT) {
                ClientContext* ctx = g_contexts[sockfd];
                
                // [I/O 动作]: 主线程将 Worker 准备好的数据发出去
                if (!ctx->write_buffer.empty()) {
                    const char* data = ctx->write_buffer.c_str();
                    size_t len = ctx->write_buffer.size();
                    size_t sent = 0;

                    // 循环写直到写完或 EAGAIN
                    while (sent < len) {
                        ssize_t ret = write(sockfd, data + sent, len - sent);
                        if (ret == -1) {
                            if (errno == EAGAIN) break; // 缓冲区满了，下次再写
                            // 错误处理...
                            break;
                        }
                        sent += ret;
                    }
                    // 移除已发送的数据
                    ctx->write_buffer.erase(0, sent);
                }

                // 如果数据发完了，重新切回“监听读”状态
                if (ctx->write_buffer.empty()) {
                    modfd(epfd, sockfd, EPOLLIN);
                } else {
                    // 没发完，继续监听写
                    modfd(epfd, sockfd, EPOLLOUT);
                }
            }
        }
    }

    close(server_sock);
    close(epfd);
    delete[] ep_events;
    // 内存清理省略...
    return 0;
}
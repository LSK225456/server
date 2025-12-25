
// 在之前的 echo_multiprogress 中，父进程充当“包工头”，来一个连接就 fork 一个“工人”（子进程）去专门服务。 而在 echo_sel
// ect 中，我们只有一个线程（包工头自己干），但他具备了“眼观六路”的能力。他拿着一张清单（fd_set），不断询问内核：“清单上
// 这 100 个 Socket，谁有数据来了？”。内核告诉他：“A 和 C 有数据了”，他就去处理 A 和 C，处理完继续问。


#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>

const int BUF_SIZE = 100;

void error_handling(const char *buf);

int main(int argc, char *argv[]) {
    int server_sock, client_sock;
    sockaddr_in server_addr, client_addr;
    timeval timeout;
    fd_set reads, copy_reads;

    socklen_t addr_size;
    int fd_max, fd_num, i;
    ssize_t str_len;
    char buf[BUF_SIZE];

    if (argc != 2) {
        printf("Usage : %s <port>\n", argv[0]);
        exit(1);
    }

    server_sock = socket(PF_INET, SOCK_STREAM, 0);
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(atoi(argv[1]));

    if (bind(server_sock, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        error_handling("bind() error");
        exit(1);
    }

    if (listen(server_sock, 5) == -1) {
        error_handling("listen() error");
    }

    FD_ZERO(&reads);            // 1. 清空集合

    // fd_set 是什么？ 它本质上是一个位图 (Bitmap)。假设这是一个 1024 位的数组，FD_SET(5, &reads) 就是把第 5 个位置置为 1。
    FD_SET(server_sock, &reads);        // 2. 把门卫（监听socket）加入集合

    fd_max = server_sock;           // 3. 记录当前最大的 fd 值（select 需要用）
    // 最大连接数限制。在 Linux 上 fd_set 默认最大只有 1024。这意味着如果不重新编译内核，这个服务器最多只能同时服务 1024 个连接。
    
    while (1) {
        
        // 为什么要 copy_reads = reads？（初学者坑点）
        // 原因：select 是一个“传入传出”参数。你传入想监控的 fd（比如 A, B, C），select 返回时，会修改这个集合，只留下有事件发生的 fd（比如只有 B）。
        // 后果：如果你不复制，直接传 reads 进去，下一次循环时，你的监控列表里就只剩 B 了，A 和 C 就丢失了。
        
        copy_reads = reads;
        timeout.tv_sec = 5;
        timeout.tv_usec = 5000;
    
        // fd_max + 1：内核需要知道扫描到哪里结束。这也暴露了 select 的第二个硬伤——效率低。即使只有 1 个 socket 活跃，内核也要从 0 扫描到 fd_max。
        // &copy_reads：关注“读”事件（是否有数据可读，或者有新连接）。
        if ((fd_num = select(fd_max + 1, &copy_reads, 0, 0, &timeout)) == -1) {
            break;
        }
        
        if (fd_num == 0) {
            continue;
        }


    
        // select 返回后，只告诉你“有 n 个 socket 活跃”，但不告诉你是哪几个
        // 。所以你必须写一个 for 循环，遍历所有可能的 fd（从 0 到 fd_max），挨个检查 FD_ISSET。
        // 这是 select 被 epoll 淘汰的根本原因。当连接数（N）很大（比如 10万），但
        // 活跃比例很低时，这个 for 循环会空转消耗大量 CPU，导致性能急剧下降。
        for (int j = 0; j < fd_max + 1; ++j) {

            if (FD_ISSET(j, &copy_reads)) {             // 只有被 select 标记为 1 的才是活跃的
                
                // 操作系统内核发现监听套接字 server_sock 的“接收缓冲区”里有了数据。对于监听
                // 套接字来说，所谓的“数据”就是已经完成了 TCP 三次握手、正在排队等待被 accept 取出的新连接。
                if (j == server_sock) {             // 情况一：门卫 Socket 活跃 -> 说明有新连接进来了！
                    addr_size = sizeof(client_addr);
                    client_sock = accept(server_sock, (sockaddr*)&client_addr, &addr_size);
                    FD_SET(client_sock, &reads);        // 注册新连接到监控列表
                    if (fd_max < client_sock) {
                        fd_max = client_sock;
                    }
                    printf("connected client: %d\n", client_sock);

                } else {            // 情况二：普通 Socket 活跃 -> 说明有数据发过来了！
                    str_len = read(j, buf, BUF_SIZE);
                    if (str_len == 0) {         // 客户端断开
                        FD_CLR(j, &reads);          // !!! 从监控列表中移除
                        close(i);                       // 关闭连接
                        printf("closed client: %d\n", j);
                    } else {
                        write(j, buf, (size_t)str_len);
                    }
                }
            }
        }
    }
    close(server_sock);

    return 0;
}


// Select 的三大原罪:
// 数量限制：默认 1024，无法支撑高并发 C10K 问题。
// 内核拷贝：每次循环都要把 reads 集合从用户态拷贝到内核态，开销大。
// 线性轮询：O(N) 复杂度，连接越多越慢。

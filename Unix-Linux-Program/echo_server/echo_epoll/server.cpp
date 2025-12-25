// epoll 的核心代码逻辑就是：建库 (create) -> 入库 (ctl ADD) -> 等待出库 (wait) -> 处理清单 (for loop)。

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/epoll.h>

const int BUF_SIZE = 100;
const int EPOLL_SIZE = 50;

void error_handling(const char *buf);

int main(int argc, char *argv[]) {
    int server_sock, client_sock;
    sockaddr_in server_addr, client_addr;

    socklen_t addr_size;
    ssize_t str_len;
    int i;
    char buf[BUF_SIZE];

    // 角色：取件篮子（数组）。
    // 含义：这是一个数组（代码中用 malloc 分配了内存）。
    // 作用：用于 epoll_wait 函数。当 epoll_wait 返回时，内核会把所有发生了
    // 的事情（比如“有连接来了”、“有数据来了”）统统抄写到这个数组里。
    // 区别：event 是你输入给内核的（我要监控谁）；ep_events 是内核输出给你的（谁活跃了）。
    epoll_event *ep_events;

//     角色：注册登记表（单张）。
// 含义：这是一个结构体，用来配置“你想监控谁”以及“监控什么动作”。
// 作用：它主要用于 epoll_ctl 函数。你想让 Epoll 帮你监控 server_sock，你就得填
// 一张表，写上“我要监控 server_sock（data.fd）”和“我要监控它的读事件（events）”，然后把这张表递给内核。
// 注意：它只是一个临时的配置工具，配完交给内核后，这个变量本身就可以重用了。
    epoll_event event;

    int event_cnt (Event Count)

    // 角色：篮子里的快递数量。
    // 含义：epoll_wait 函数的返回值。
    // 作用：告诉你刚才那个 ep_events 数组里，前几个位置是有效的。如果返回 3，说明数
    // 组的第 0, 1, 2 个元素存放了有效的事件，后面的都是垃圾数据，不用看。
    int epfd, event_cnt;

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

    // 在 select 中，我们只有一个函数 select()。但在 Epoll 中，过程被拆分成了三个步骤：建档、注册、等待。
    // 创建一个 epoll 实例（在内核中申请空间）,参数 EPOLL_SIZE (50) 在现代 Linux 内核中已被忽略，填大于 0 即可
    //     快递站的门牌号。含义：这是你调用 epoll_create 后，内核返给你的一个 ID。
    // 作用：以后你想往这个 Epoll 实例里添加监控对象、或者等待事件，都必须拿着这个 ID 去找内核。它代表了内核里
    // 那棵红黑树（存储监控名单）和就绪链表（存储活跃事件）。
    epfd = epoll_create(EPOLL_SIZE);


    // select 每次调用都要在内核中临时建立数据结构，而 epoll 是先在内核里“盖一栋房子”（红黑树），以后所有的监控对象都存在这里，不用搬来搬去。
    ep_events = (epoll_event*)malloc(sizeof(epoll_event) * EPOLL_SIZE);

    event.events = EPOLLIN;         // 监听读事件 (LT模式)
    event.data.fd = server_sock;
    epoll_ctl(epfd, EPOLL_CTL_ADD, server_sock, &event);    // 往“房子”里添加一个监控对象：server_sock

    while (1) {
        // 阻塞等待，直到有事件发生
    // ep_events: 这是一个“空篮子”，内核会把发生的事件都丢到这个篮子里
    // select 返回时，你需要遍历 0 到 1024 所有位，去查找谁变了。
    // epoll_wait 返回时，只给你“活跃”的 fd。如果监控了 100 万个连接，只有 3 个发来数据，event_cnt 就是3,
    //  ep_events 数组里就只有这 3 个事件。你只需要处理这 3 个，完全不需要管剩下的 999997 个。
        event_cnt = epoll_wait(epfd, ep_events, EPOLL_SIZE, -1);
        if (event_cnt == -1) {
            puts("epoll_wait() error");
            break;
        }
        // 只需要遍历前 event_cnt 个
        for (int i = 0; i < event_cnt; ++i) {
            if (ep_events[i].data.fd == server_sock) {  // 1. 门卫 Socket 活跃 -> 处理新连接
                addr_size = sizeof(client_addr);
                client_sock = accept(server_sock, (sockaddr*)&client_addr, &addr_size);

                // 注册新连接到 epoll 实例
                event.events = EPOLLIN;
                event.data.fd = client_sock;
                epoll_ctl(epfd, EPOLL_CTL_ADD, client_sock, &event);
                printf("connect client: %d\n", client_sock);

            } else {        // 2. 普通 Socket 活跃 -> 处理数据

                // 直接读取数据。因为是 LT 模式，只要缓冲区还有数据，下一轮循环还会通知你，
            // 所以这里 read 一次也没关系（虽然不推荐，但不会死锁）。
                str_len = read(ep_events[i].data.fd, buf, BUF_SIZE);

                if (str_len == 0) {     // 客户端断开
                    // 重要：先从 epoll 中删除，再 close
                    epoll_ctl(epfd, EPOLL_CTL_DEL, ep_events[i].data.fd, NULL);
                    close(ep_events[i].data.fd);
                    printf("closed client: %d\n", ep_events[i].data.fd);
                } else {
                    write(ep_events[i].data.fd, buf,str_len);
                }
            }
        }
    }
    close(server_sock);
    close(epfd);

    return 0;
}


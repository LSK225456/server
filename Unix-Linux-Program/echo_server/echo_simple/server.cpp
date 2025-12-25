#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>      // 网络编程离不开地址处理，比如把 IP 地址字符串转成二进制，或者处理字节序（大小端问题），这个库提供了 sockaddr_in 结构体和 htons 等函数。
#include <sys/socket.h>
//  改为并发服务器
#include <sys/wait.h> // 引入 wait 库，用于处理僵尸进程
#include <signal.h>   // 引入信号库

const int BUF_SIZE = 1024;

// 改为并发服务器:僵尸进程处理函数：当子进程结束时，操作系统会发送 SIGCHLD 信号
// 我们需要在这个函数里回收子进程资源，否则子进程会变成“僵尸”占用系统名额
void read_childproc(int sig) {
    pid_t pid;
    int status;
    // waitpid(-1, ...): 等待任意子进程退出
    // WNOHANG: 非阻塞模式，如果没有子进程结束立刻返回，不卡住主流程
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("Removed proc id: %d \n", pid);
    }
}

void error_handling(const char *message);

// 接收一个参数，argv[1]为端口号
int main(int argc, char *argv[]) {
    int server_socket;
    int client_sock;

    char message[BUF_SIZE];
    ssize_t str_len;
    int i;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t client_addr_size;

    pid_t pid; // 用于存储 fork 返回的进程 ID

    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // 改为并发服务器: 设置信号处理，防止僵尸进程
    struct sigaction act;
    act.sa_handler = read_childproc; // 指定信号处理函数
    sigemptyset(&act.sa_mask);       // 初始化信号集
    act.sa_flags = 0;
    // 注册信号：当收到子进程结束信号(SIGCHLD)时，调用 read_childproc
    sigaction(SIGCHLD, &act, 0);

    // PF_INET: 告诉内核要用 IPv4 协议族
    // SOCK_STREAM: 告诉内核我们需要一个面向连接的、可靠的、字节流的服务。翻译过来就是 TCP。如果你要用 UDP，这里就是 SOCK_DGRAM。
    // 0: 自动选择协议。在 PF_INET 和 SOCK_STREAM 组合下，系统知道你只能选 TCP，所以填 0 即可。
    server_socket = socket(PF_INET, SOCK_STREAM, 0); // 创建IPv4 TCP socket
    if (server_socket == -1) {
        error_handling("socket() error");
    }

    // 地址信息初始化
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; // IPV4 地址族
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 使用INADDR_ANY分配服务器的IP地址
    server_addr.sin_port = htons(atoi(argv[1])); // 端口号由第一个参数设置

    // 分配地址信息
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(sockaddr)) == -1) {
        error_handling("bind() error");
    }

    // 监听连接请求，最大同时连接数为5
    if (listen(server_socket, 5) == -1) {
        error_handling("listen() error");
    }

    // client_addr_size = sizeof(client_addr);
    // for (i = 0; i < 5; ++i) {
    while(1){       // 2. 将 for 循环改为 while(1)，因为服务器通常是永不停止的
        client_addr_size = sizeof(client_addr); // 每次 accept 前必须重置！
        // 受理客户端连接请求
        client_sock = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_size);
        if (client_sock == -1) {
            continue; // 如果连接失败，继续等待下一个，不要退出程序
            // error_handling("accept() error");
        } else {
            printf("Connect client %d\n", i + 1);
        }
        
        // 开始并发处理：创建子进程
        // 步骤 1: 父进程运行到这里
        pid = fork(); 
        // 【裂变发生！】
        // 此时系统里有了两个完全一样的进程，正在运行同一行代码。
        // 但是！pid 的值在两个进程里不一样。

        // ==========================================
        //  平行宇宙 A：父进程 (PID 32620) 的视角
        // ==========================================
        // fork() 返回给父进程的是子进程的 ID (比如 24700)，所以 pid > 0

        if (pid == -1) {
            close(client_sock);
            continue;
        }

        // --- 子进程区域 (只运行负责与客户端通信的代码) ---
        if (pid == 0) { 
//  平行宇宙 B：子进程 (PID 24700) 的视角
// fork() 返回给子进程的是 0，所以 pid == 0
            // 父进程一看：我的 pid 是 24700，不等于 0。
            // 结果：完全跳过这个大括号！里面的 while 循环根本不执行！
            // 重点 A：子进程不需要监听 socket，它只需要处理刚接手的 client_sock
            // 关闭是为了安全，防止子进程误操作监听端口
            close(server_socket); 

            char message[BUF_SIZE];
            int str_len;

            // 循环读取数据（回声业务逻辑）
            while ((str_len = read(client_sock, message, BUF_SIZE)) != 0) {
                write(client_sock, message, str_len);
                // 为了演示效果，可以在服务端打印一下
                message[str_len] = '\0';
                printf("Client(proc %d): %s", getpid(), message);
            }

            // 业务结束，关闭连接
            close(client_sock); 
            puts("Client disconnected...");
            
            // 子进程必须退出！否则它会跑出这个 if 块，进入外层的 while(1) 变成另一个父进程
            return 0; 
        }
        // ---------------------------------------------

        // --- 父进程区域 ---
        else {
            // 重点 B：父进程必须关闭 client_sock！
            // 原理：fork 会把文件描述符复制一份。
            // 此时 client_sock 有两个引用（父进程1个，子进程1个）。
            // 父进程不负责通信，如果不关闭，引用计数不会归零，TCP连接将永远断不开。
            // 父进程执行这里
            close(client_sock); // 扔掉连接，我不负责聊天
        }

        // // 读取来自客户端的数据
        // while ((str_len = read(client_sock, message, BUF_SIZE)) != 0) {
        //     // 向客户端传输数据
        //     write(client_sock, message, (size_t)str_len);
        //     message[str_len] = '\0';
        //     printf("client %d: message %s", i + 1, message);
        // }
    }
    // 关闭连接
    close(server_socket);

    printf("echo server\n");
    return 0;
}

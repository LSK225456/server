#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h> // Linux 线程核心库
#include <semaphore.h> // Linux 信号量核心库
#include <unistd.h>  // 用于 sleep 模拟耗时
#include <iostream>

// =========================================================
// 第一部分：基础设施 (原书 locker.h 的实现)
// 作用：将底层的 OS 原语封装成类，利用 RAII 机制管理资源
// =========================================================

/* 封装信号量 (Semaphore)
 * 面试考点：信号量与条件变量的区别？
 * 信号量像是一个计数器（餐厅门口的叫号机），用来控制资源的访问数量。
 * 这里用于表示“任务队列中待处理的任务数量”。
 */
class sem {
public:
    sem() {
        /* sem_init 参数说明：
         * 1. &m_sem: 信号量指针
         * 2. 0:      pshared, 0表示线程间共享，非0表示进程间共享(多进程编程时用)
         * 3. 0:      value, 信号量的初始值
         */
        if (sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();
        }
    }
    ~sem() {
        sem_destroy(&m_sem); // 销毁信号量，释放内核资源
    }
    
    /* P操作 (Wait)
     * 逻辑：如果信号量值 > 0，则减 1 并立即返回；
     * 如果信号量值 == 0，则阻塞当前线程，直到信号量变为 > 0。
     * 作用：在 run() 中使用，如果没有任务，工作线程就在这里睡觉，不占 CPU。
     */
    bool wait() {
        return sem_wait(&m_sem) == 0;
    }

    /* V操作 (Post)
     * 逻辑：将信号量值 + 1，并唤醒一个正在 wait 的线程。
     * 作用：在 append() 中使用，添加任务后通知工作线程“有活干了”。
     */
    bool post() {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

/* 封装互斥锁 (Mutex)
 * 作用：保护共享资源（即 m_workqueue），确保同一时刻只有一个线程能操作队列。
 */
class locker {
public:
    locker() {
        /* pthread_mutex_init 参数：
         * 2. NULL: 使用默认互斥锁属性（快速互斥锁）
         */
        if (pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
    }
    ~locker() {
        pthread_mutex_destroy(&m_mutex);
    }
    
    // 上锁：进入临界区
    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    
    // 解锁：离开临界区
    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

private:
    pthread_mutex_t m_mutex;
};

// =========================================================
// 第二部分：线程池核心实现 (原书 threadpool.h)
// =========================================================

/* 模板参数 T 是任务类，后端开发中通常是 HttpContext 或 TaskStruct */
template< typename T >
class threadpool
{
public:
    /* thread_number: 线程池常驻线程数（通常设置为 CPU 核数 或 核数*2）
     * max_requests:  队列最大堆积请求数，防止流量暴涨撑爆内存
     */
    threadpool( int thread_number = 8, int max_requests = 10000 );
    ~threadpool();
    
    /* 往请求队列中添加任务 (生产者行为) */
    bool append( T* request );

private:
    /* 工作线程运行的入口函数 */
    static void* worker( void* arg );
    
    /* 线程池实际的执行逻辑 */
    void run();

private:
    int m_thread_number;        // 线程数
    int m_max_requests;         // 最大请求数
    pthread_t* m_threads;       // 线程ID数组（类似 PID，但用于线程）
    std::list< T* > m_workqueue;// 请求队列（共享资源，需加锁）
    locker m_queuelocker;       // 保护队列的锁
    sem m_queuestat;            // 信号量：指示是否有任务
    bool m_stop;                // 停止标志
};

// --- 构造函数 ---
template< typename T >
threadpool< T >::threadpool( int thread_number, int max_requests ) :
    m_thread_number( thread_number ), m_max_requests( max_requests ),
    m_stop( false ), m_threads( NULL )
{
    if(( thread_number <= 0 ) || ( max_requests <= 0 )) throw std::exception();

    // 1. 分配线程ID数组内存
    m_threads = new pthread_t[ m_thread_number ];
    if(!m_threads) throw std::exception();

    // 2. 循环创建线程
    for ( int i = 0; i < thread_number; ++i )
    {
        printf( "[Main] create the %dth thread\n", i );
        
        /* [核心函数] pthread_create
         * 参数1: 指向线程ID的指针，创建成功后会填入ID
         * 参数2: 线程属性 (NULL 代表默认)
         * 参数3: 线程运行的函数指针 (必须是静态函数或全局函数)
         * 参数4: 传递给运行函数的参数 (这里传入 this 指针，让静态函数能操作类成员)
         */
        if( pthread_create( m_threads + i, NULL, worker, this ) != 0 )
        {
            delete [] m_threads;
            throw std::exception();
        }
        
        /* [核心函数] pthread_detach
         * 作用：将线程设置为“脱离状态”。
         * 意义：如果不 detach，主线程必须调用 pthread_join 来回收子线程资源，否则会僵尸线程。
         * 后端服务中，worker 线程通常生命周期很长，我们不希望主线程阻塞等待它们，
         * 所以设置为 detach，让操作系统自动回收资源。
         */
        if( pthread_detach( m_threads[i] ) )
        {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

// --- 析构函数 ---
template< typename T >
threadpool< T >::~threadpool()
{
    delete [] m_threads;
    m_stop = true;
}

// --- 添加任务 (Producer) ---
template< typename T >
bool threadpool< T >::append( T* request )
{
    // 1. 上锁：因为 m_workqueue 是被所有线程共享的
    m_queuelocker.lock();
    
    // 2. 检查队列是否满了（高并发保护）
    if ( m_workqueue.size() > m_max_requests )
    {
        m_queuelocker.unlock();
        return false;
    }
    
    // 3. 放入任务
    m_workqueue.push_back( request );
    m_queuelocker.unlock();
    
    // 4. [关键] 信号量 +1，唤醒一个正在 wait 的 worker 线程
    m_queuestat.post();
    return true;
}

// --- 静态工作函数 ---
/* 为什么必须是 static？
 * C++ 类的成员函数隐含一个 this 指针参数，而 pthread_create 要求函数签名必须是 void* func(void*)。
 * 所以必须用 static 消除 this 指针，然后通过 arg 手动传入 this。
 */
template< typename T >
void* threadpool< T >::worker( void* arg )
{
    threadpool* pool = ( threadpool* )arg;
    pool->run(); // 跳板：进入实际的成员函数
    return pool;
}

// --- 消费者逻辑 (核心中的核心) ---
template< typename T >
void threadpool< T >::run()
{
    /* [学习目标]：理解 run() 函数里为什么有个 while(true)?
     * 答：这是线程池的本质——“池化”。线程创建后不销毁，而是循环利用。
     * 它一直循环等待任务，处理完一个，回来接着等下一个。
     */
    while ( ! m_stop )
    {
        // 1. 等待信号量 (P操作)
        // 如果队列为空，线程会在这里阻塞（挂起），不消耗 CPU。
        // 一旦 m_queuestat.post() 被调用，这里会返回。
        m_queuestat.wait();
        
        // 2. 抢锁
        // 虽然信号量醒了，但可能有多个线程同时醒来（惊群效应），
        // 或者单纯为了安全操作 std::list，必须加锁。
        m_queuelocker.lock();
        
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        
        // 3. 取任务 (从队头取)
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        
        // 4. 立即解锁
        // [后端准则]：锁的粒度要越小越好。取出任务后立刻解锁，
        // 让其他线程可以去取下一个任务，不要占着锁执行耗时的 process()。
        m_queuelocker.unlock();
        
        if ( ! request ) continue;
        
        // 5. 执行业务逻辑
        // 这里体现了“半同步/半反应堆”：
        // 主线程(反应堆)只管塞任务，这里(同步线程)负责处理具体逻辑。
        request->process();
    }
}

// =========================================================
// 第三部分：Main 模拟测试
// =========================================================

// 模拟一个 HTTP 请求任务类
struct WebTask {
    int id;
    void process() {
        // 模拟 CPU 密集型或 IO 密集型操作
        printf("Thread[%lu] is processing Task[%d]...\n", pthread_self(), id);
        usleep(50000); // 模拟耗时 50ms
    }
};

int main() {
    try {
        // 1. 创建线程池：4个线程，最大允许100个任务积压
        threadpool<WebTask> pool(4, 100);

        // 2. 模拟高并发请求到来 (Main 线程充当 Epoll Reactor 角色)
        for(int i = 0; i < 20; ++i) {
            // 注意：实际项目中 request 通常是 new 出来的对象，生命周期需注意
            // 这里为了演示简单，用了 new，实际中需要考虑 delete (通常在 process 结束后 delete)
            WebTask* task = new WebTask; 
            task->id = i;
            
            // 3. 将任务抛入线程池
            // 这就是 Reactor 模式中的 "Event Demultiplexer" 分发事件
            pool.append(task);
            printf("[Main] Append task %d\n", i);
            
            usleep(10000); // 模拟请求间隔
        }

        // 等待子线程处理完 (仅用于演示，实际 Server 是死循环)
        sleep(5); 
        printf("[Main] Server shutdown.\n");
        
    } catch (...) {
        printf("Exception caught.\n");
    }

    return 0;
}
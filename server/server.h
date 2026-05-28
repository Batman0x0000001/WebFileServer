/*  文件说明：
 *  1. 定义 WebServer，负责服务端启动后的 socket、epoll、信号管道和线程池生命周期。
 *  2. main.cpp 按顺序调用本类完成监听 fd、epoll 实例、信号管道、信号处理和线程池初始化。
 *  3. waitEpoll() 是主事件循环，会把 accept、signal、recv、send 事件封装为 EventBase 派生对象投递给线程池。
 *  4. 信号处理函数只写入 socketpair，真正的定时清理连接逻辑由普通事件处理代码执行。
 */
#ifndef WEBSERVER_H
#define WEBSERVER_H
#include <signal.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <stdexcept>
#include <errno.h>
#include <memory>

#include "../threadpool/threadpool.h"

#define MAX_RESEVENT_SIZE 1024   // 事件的最大个数

class WebServer{
public:
    WebServer();
    
    // 创建套接字等待客户端连接，并开启监听。
    int createListenFd(int port, const char* ip = nullptr );
    
    // 创建 epoll 实例，用作整个服务端的 I/O 多路复用核心。
    int createEpoll();
    
    // 向 epoll 中添加监听套接字，后续 accept 事件由线程池处理。
    int epollAddListenFd();

    // 创建并注册信号管道，把异步信号统一转换为 epoll 可读事件。
    int epollAddEventPipe();

    // 注册 TERM 和 ALARM 等信号处理。
    int addHandleSig(int signo = -1);

    // 信号处理函数只写管道，不做复杂逻辑，避免异步信号安全问题。
    static void setSigHandler(int signo);

    // 主事件循环：等待 epoll 事件并投递到线程池。
    int waitEpoll();

    // 创建工作线程池，用于处理 accept、recv、send、signal 事件。
    int createThreadPool(int threadNum = 8);
    
    ~WebServer();
private:
    int m_listenfd;                   // 服务端的套接字
    sockaddr_in m_serverAddr;         // 服务端套接字绑定的地址信息
    static int s_epollfd;             // I/O 复用的 epoll 例程文件描述符
    static bool s_isStop;             // 是否暂停服务器

    static int s_eventPipe[2];        // 用于统一事件源传递信号的管道

    epoll_event resEvents[MAX_RESEVENT_SIZE]; // 保存 epoll_wait 结果的数组
    
    std::unique_ptr<ThreadPool> m_threadPool;
};

#endif

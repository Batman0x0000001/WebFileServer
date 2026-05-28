/*  文件说明：
 *  1. 定义 epoll 事件投递到线程池后的具体事件对象。
 *  2. EventBase 只是多态基类，不保存连接状态；连接状态统一由 ConnectionManager 管理。
 *  3. AcceptConn 处理监听 fd 的新连接，HandleSig 处理信号管道中的定时器信号。
 *  4. HandleRecv 是可读事件入口，负责取连接锁和请求/响应对象，再委托 RequestProcessor。
 *  5. HandleSend 是可写事件入口，负责取连接锁和响应对象，再委托 ResponseSender。
 */
#ifndef EVENT_HANDLER_H
#define EVENT_HANDLER_H

// 所有线程池事件的基类，派生类通过 process() 执行具体事件逻辑。
class EventBase{
public:
    EventBase(){

    }
    virtual ~EventBase(){

    }

public:
    // 不同类型事件中重写该函数，执行不同的处理方法。
    virtual void process() = 0;

};



// 监听 fd 可读时创建该事件，在线程池中 accept 所有待处理连接。
class AcceptConn : public EventBase{
public:
    AcceptConn(int listenFd, int epollFd): m_listenFd(listenFd), m_epollFd(epollFd){ };
    virtual ~AcceptConn(){ };

public:
    virtual void process() override;

private:
    int m_listenFd;              // 服务端监听套接字
    int m_epollFd;               // 接收连接后加入的 epoll 实例
};

// 信号管道可读时创建该事件，用于处理 SIGALRM 等异步信号。
class HandleSig : public EventBase{
public:
    HandleSig(int epollFd, int pipeFd) : EventBase(), m_epollFd(epollFd), m_pipeFd(pipeFd){ };
    virtual ~HandleSig(){ };
public:
    virtual void process() override;

private:
    int m_epollFd;    // epoll 文件描述符，用于删除超时连接
    int m_pipeFd;     // 信号管道的读端文件描述符
};



// 客户端 fd 可读时创建该事件，推进请求接收、解析和路由。
class HandleRecv : public EventBase{
public:
    HandleRecv(int clientFd, int epollFd) : m_clientFd(clientFd), m_epollFd(epollFd){ };
    virtual ~HandleRecv(){ };
public:
    virtual void process() override;

private:
    int m_clientFd;   // 客户端套接字，从该客户端读取数据
    int m_epollFd;    // epoll 文件描述符，在需要重置事件或关闭连接时使用


};

// 客户端 fd 可写时创建该事件，推进响应构造和发送。
class HandleSend : public EventBase{
public:
    HandleSend(int clientFd, int epollFd) : m_clientFd(clientFd), m_epollFd(epollFd){ };
    virtual ~HandleSend(){ };

public:
    virtual void process() override;

private:
    int m_clientFd;   // 客户端套接字，向该客户端写数据
    int m_epollFd;    // epoll 文件描述符，在需要重置事件或关闭连接时使用
};

#endif

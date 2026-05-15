/*  文件说明：
 *  1. 当主线程监听到事件时，根据事件类型构建一个特定类型的事件对象，加入线程池的待处理事件对象中等待被处理
 *  2. 线程池中会调用事件的 process 函数，函数中会根据对消息的处理状态执行对应的操作
 *  3. EventBase 表示所有事件的基类，其中包含两个 map 静态成员 requestStatus 和 responseStatus，使用套接字作为key，保存该套接字对应的请求消息(Request)或响应消息(Response)处理的状态
 *  4. AcceptConn 中的 process 函数用于接收新的连接并加入 epoll_wait 中
 *  5. HandleSig 中的 process 函数用于处理产生的各种事件
 *  6. 由于是静态成员，即使请求消息或响应消息没有接收完整或退出，下次产生事件时还会根据处理的状态继续执行下一步操作
 *  7. requestStatus 保存所有套接字当前对请求消息接收并处理了多少，根据请求消息的状态在 process 函数中对请求消息继续处理
 *  8. responseStatus 保存所有套接字当前对响应消息构建并发送了多少，根据请求消息的状态在 process 函数中对请求消息继续处理
 *  9. 如果某个套接字没有任何事件产生，requestStatus 和 responseStatus 中保存的事件会被清空
 *  🔄 核心思想：事件驱动 + 非阻塞 IO + 状态保留
 *  服务器用 epoll 监听套接字事件，每当某个连接产生事件，就构建对应的 EventBase 派生类对象，并将其交给线程池执行 process()。
 */
#ifndef MYEVENT_H
#define MYEVENT_H
#include <dirent.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <ctime>
#include <signal.h>
#include <memory>
#include <mutex>

#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <unistd.h>

#include "../message/message.h"
#include "../utils/utils.h"

// 所有事件的基类
class EventBase{
public:
    EventBase(){

    }
    virtual ~EventBase(){

    }
protected:
    // 保存文件描述符对应的请求的状态，因为一个连接上的数据可能非阻塞一次读取不完，所以保存到这里，当该连接上有新数据时，可以继续读取并处理
    static std::unordered_map<int, Request> requestStatus;

    // 保存文件描述符对应的发送数据的状态，一次proces中非阻塞的写数据可能无法将数据全部传过去，所以保存当前数据发送的状态，可以继续传递数据
    static std::unordered_map<int, Response> responseStatus;
    //所以即使一次 read() 或 send() 没完成，也能“断点续传”。

    // 保存每个连接最后一次活跃的时间，用于定时清理空闲连接
    static std::unordered_map<int, time_t> activeTime;

    // 保护连接活跃时间表，避免定时器和读写事件并发访问
    static std::mutex activeTimeLocker;
    static std::mutex statusLocker;
    static std::unordered_map<int, std::shared_ptr<std::mutex> > connLockers;

    // 更新连接的最后活跃时间
    static void updateActiveTime(int fd);

    // 删除连接相关的请求、响应和活跃时间状态
    static void eraseConnStatus(int fd);

    // 关闭一个超时或异常连接
    static void closeConn(int epollFd, int fd);
    static std::shared_ptr<std::mutex> getConnLocker(int fd);

    // 清理超过空闲时间的连接
    static void closeExpiredConn(int epollFd, int idleTimeout);

public:
    // 不同类型事件中重写该函数，执行不同的处理方法
    virtual void process() = 0;

};



// 用于接受客户端连接的事件
class AcceptConn : public EventBase{
public:
    AcceptConn(int listenFd, int epollFd): m_listenFd(listenFd), m_epollFd(epollFd){ };
    virtual ~AcceptConn(){ };

public:
    virtual void process() override;

private:
    int m_listenFd;              // 保存监听套接字 
    int m_epollFd;               // 接收连接后加入的 epoll
};

// 用于处理信号的事件，暂时没有处理信号事件需要，没有具体实现实现
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



// 处理客户端发送的请求
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

// 处理向客户端发送数据
class HandleSend : public EventBase{
public:
    HandleSend(int clientFd, int epollFd) : m_clientFd(clientFd), m_epollFd(epollFd){ };
    virtual ~HandleSend(){ };

public:
    virtual void process() override;
    
    // 用于构建状态行，参数分别表示状态行的三个部分
    std::string getStatusLine(Response &response, const std::string &httpVersion, const std::string &statusCode, const std::string &statusDes);

    // 根据状态码获取对应的状态描述
    std::string getStatusDesc(HTTPSTATUS statusCode);

    // 构建空消息体响应，目前主要用于重定向
    void buildEmptyResponse(Response &response, HTTPSTATUS statusCode, const std::string &redirectLoction = "");

    // 构建 HTML 消息体响应，用于正常页面和错误页面
    void buildHtmlResponse(Response &response, HTTPSTATUS statusCode, const std::string &msgBody, const std::string &extraHeader = "");

    // 构建文件响应头，文件内容仍由 sendfile 发送
    void buildFileResponseHeader(Response &response, unsigned long fileSize, const std::string &downloadFilename);

    // 以下两个函数用来构建文件列表的页面，最终结果保存到 fileListHtml 中
    void getFileListPage(std::string &fileListHtml, const std::string &username);

    void getFileVec(const std::string &dirName, std::vector<std::string> &resVec);

    // 构建头部字段：
    // contentLength        : 指定消息体的长度
    // contentType          : 指定消息体的类型
    // redirectLoction = "" : 如果是重定向报文，可以指定重定向的地址。空字符串表示不添加该首部。
    // contentRange = ""    : 如果是下载文件的响应报文，指定当前发送的文件范围。空字符串表示不添加该首部。
    std::string getMessageHeader(const std::string &contentLength, const std::string &contentType, const std::string &redirectLoction = "", const std::string &contentRange = "", const std::string &extraHeader = "", const std::string &downloadFilename = "");


private:
    int m_clientFd;   // 客户端套接字，向该客户端写数据
    int m_epollFd;    // epoll 文件描述符，在需要重置事件或关闭连接时使用
};

#endif

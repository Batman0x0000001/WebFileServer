#include <mutex>
#include <cerrno>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "event_handler.h"
#include "connection_manager.h"
#include "epoll_util.h"
#include "../config/config.h"
#include "../utils/log.h"
#include "../request/request_processor.h"
#include "../response/response_sender.h"

// 用于接受客户端连接的事件
void AcceptConn::process(){
    while(1){
        sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        int accFd = accept(m_listenFd, (sockaddr*)&clientAddr, &clientAddrLen);
        if(accFd == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                return;
            }
            std::cout << outHead("error") << "接受新连接失败" << std::endl;
            return;
        }

        {
            std::lock_guard<std::mutex> locker(ConnectionManager::activeTimeLocker);
            if(static_cast<int>(ConnectionManager::activeTime.size()) >= AppConfig::instance().maxConnection()){
                close(accFd);
                std::cout << outHead("error") << "连接数达到上限，拒绝新连接" << std::endl;
                continue;
            }
        }

        setNonBlocking(accFd);
        addWaitFd(m_epollFd, accFd, true, true);
        ConnectionManager::updateActiveTime(accFd);
        std::cout << outHead("info") << "接受新连接 " << accFd << " 成功" << std::endl;
    }
}

// 处理信号事件
void HandleSig::process(){
    int sigBuf[32];
    int readLen = 0;

    // 读取信号管道中的所有信号，避免管道中残留旧信号
    while((readLen = recv(m_pipeFd, sigBuf, sizeof(sigBuf), 0)) > 0){
        int sigNum = readLen / sizeof(int);
        for(int i = 0; i < sigNum; ++i){
            if(sigBuf[i] == SIGALRM){
                ConnectionManager::closeExpiredConn(m_epollFd, AppConfig::instance().idleTimeout());
                alarm(AppConfig::instance().timerInterval());
                std::cout << outHead("info") << "定时器事件处理完成" << std::endl;
            }
        }
    }
}

// 处理客户端发送的请求
void HandleRecv::process(){
    std::shared_ptr<std::mutex> connLocker = ConnectionManager::getConnLocker(m_clientFd);
    std::lock_guard<std::mutex> connGuard(*connLocker);
    std::shared_ptr<Request> requestPtr;
    std::shared_ptr<Response> responsePtr;
    {
        std::lock_guard<std::mutex> statusGuard(ConnectionManager::statusLocker);
        if(ConnectionManager::requestStatus[m_clientFd] == nullptr){
            ConnectionManager::requestStatus[m_clientFd].reset(new Request);
        }
        if(ConnectionManager::responseStatus[m_clientFd] == nullptr){
            ConnectionManager::responseStatus[m_clientFd].reset(new Response);
        }
        requestPtr = ConnectionManager::requestStatus[m_clientFd];
        responsePtr = ConnectionManager::responseStatus[m_clientFd];
    }
    Request &request = *requestPtr;
    Response &response = *responsePtr;
    std::cout << outHead("info") << "开始处理客户端 " << m_clientFd << " 的一个 HandleRecv 事件" << std::endl;
    ConnectionManager::updateActiveTime(m_clientFd);
    RequestProcessor::process(m_clientFd, m_epollFd, request, response);

    
    if(request.status == HANDLE_COMPLETE){     // 如果请求处理完成，将该套接字对应的请求删除
        std::cout << outHead("info") << "客户端 " << m_clientFd << " 的请求消息处理成功" << std::endl;
        {
            std::lock_guard<std::mutex> statusGuard(ConnectionManager::statusLocker);
            ConnectionManager::requestStatus.erase(m_clientFd);
        }
    }else if(request.status == HANDLE_ERROR){        
        // 请求处理错误，关闭该文件描述符，将该套接字对应的请求删除，从监听列表中删除该文件描述符
        std::cout << outHead("error") << "客户端 " << m_clientFd << " 的请求消息处理失败，关闭连接" << std::endl;
        ConnectionManager::closeConn(m_epollFd, m_clientFd);
    }
    
}

// 处理向客户端发送数据
void HandleSend::process(){
    std::shared_ptr<std::mutex> connLocker = ConnectionManager::getConnLocker(m_clientFd);
    std::lock_guard<std::mutex> connGuard(*connLocker);
    std::shared_ptr<Response> responsePtr;
    {
        std::lock_guard<std::mutex> statusGuard(ConnectionManager::statusLocker);
        auto responseIter = ConnectionManager::responseStatus.find(m_clientFd);
        if(responseIter == ConnectionManager::responseStatus.end() || responseIter->second == nullptr){
            std::cout << outHead("info") << "客户端 " << m_clientFd << " 没有要处理的响应消息" << std::endl;
            return;
        }
        responsePtr = responseIter->second;
    }
    Response &response = *responsePtr;
    std::cout << outHead("info") << "开始处理客户端 " << m_clientFd << " 的一个 HandleSend 事件" << std::endl;
    ConnectionManager::updateActiveTime(m_clientFd);
    ResponseSender::sendResponse(m_clientFd, response);
    

    // 判断发送最终状态执行特定的操作
    if(response.status == HANDLE_COMPLETE){
        // 完成发送数据后删除该响应
        bool closeAfterSend = response.closeAfterSend;
        {
            std::lock_guard<std::mutex> statusGuard(ConnectionManager::statusLocker);
            ConnectionManager::responseStatus.erase(m_clientFd);
        }
        std::cout << outHead("info") << "客户端 " << m_clientFd << " 的响应报文发送成功" << std::endl;
        if(closeAfterSend){
            ConnectionManager::closeConn(m_epollFd, m_clientFd);
        }else{
            modifyWaitFd(m_epollFd, m_clientFd, true, true, false);
        }
        return;
    }else if(response.status == HANDLE_ERROR){
        // 如果发送失败，删除该响应，删除监听该文件描述符，关闭连接
        ConnectionManager::closeConn(m_epollFd, m_clientFd);
        std::cout << outHead("error") << "客户端 " << m_clientFd << " 的响应报文发送失败，关闭相关的文件描述符" << std::endl;
        return;
    }else{                      // 如果不是完成了数据传输或出错，应该重置 EPOLLSHOT 事件，保证写事件可以继续产生，继续传输数据
        modifyWaitFd(m_epollFd, m_clientFd, true, true, true);

        // 退出函数，当执行失败时或数据传输完成时才需要关闭文件
        return;
    }

}

#include "./fileserver/fileserver.h"
#include "./config/config.h"
#include "./database/mysqlclient.h"
#include "./cache/redisclient.h"
#include <signal.h>
#include <unistd.h>

int main(void){
    AppConfig::instance().load("config/server.conf");
    setLogLevel(AppConfig::instance().logLevel());
    initLogFile(AppConfig::instance().logFile());
    signal(SIGPIPE, SIG_IGN);
    if(!MysqlClient::instance().init()){
        std::cout << outHead("error") << "MySQL 初始化失败，服务器退出" << std::endl;
        return -1;
    }
    if(!RedisClient::instance().init()){
        std::cout << outHead("error") << "Redis 初始化失败，服务器退出" << std::endl;
        return -2;
    }

    WebServer webserver;

    // 创建线程池
    int ret = webserver.createThreadPool(AppConfig::instance().threadNum());
    if(ret != 0){
        std::cout << outHead("error") << "创建线程池失败" << std::endl;
        return -3;
    }

    // 初始化用于监听的套接字
    ret = webserver.createListenFd(AppConfig::instance().port());
    if(ret != 0){
        std::cout << outHead("error") << "创建并初始化监听套接字失败" << std::endl;
        return -4;
    }

    // 初始化监听的epoll例程
    ret = webserver.createEpoll();
    if(ret != 0){
        std::cout << outHead("error") << "初始化监听的epoll例程失败" << std::endl;
        return -5;
    }

    // 初始化信号管道，并将定时器事件统一交给 epoll 处理
    ret = webserver.epollAddEventPipe();
    if(ret != 0){
        std::cout << outHead("error") << "epoll 添加信号管道失败" << std::endl;
        return -6;
    }
    ret = webserver.addHandleSig();
    if(ret != 0){
        std::cout << outHead("error") << "初始化信号处理失败" << std::endl;
        return -7;
    }
    alarm(AppConfig::instance().timerInterval());

    // 向 epoll 中添加监听套接字
    ret = webserver.epollAddListenFd();
    if(ret != 0){
        std::cout << outHead("error") << "epoll 添加监听套接字失败" << std::endl;
        return -8;
    }

    // 开启监听并处理请求
    ret = webserver.waitEpoll();
    if(ret != 0){
        std::cout << outHead("error") << "epoll 例程监听失败" << std::endl;
        return -9;
    }
    
    return 0;
}

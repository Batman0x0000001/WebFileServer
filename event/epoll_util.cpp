#include "epoll_util.h"

#include <iostream>
#include <sys/epoll.h>
#include <sys/fcntl.h>

#include "../utils/log.h"

int addWaitFd(int epollFd, int newFd, bool edgeTrigger, bool isOneshot){
    epoll_event event;
    event.data.fd = newFd;
    event.events = EPOLLIN;
    if(edgeTrigger){
        event.events |= EPOLLET;
    }
    if(isOneshot){
        event.events |= EPOLLONESHOT;
    }

    int ret = epoll_ctl(epollFd, EPOLL_CTL_ADD, newFd, &event);
    if(ret != 0){
        std::cout << outHead("error") << "添加文件描述符失败" << std::endl;
        return -1;
    }
    return 0;
}

int modifyWaitFd(int epollFd, int modFd, bool edgeTrigger, bool resetOneshot, bool addEpollout){
    epoll_event event;
    event.data.fd = modFd;
    event.events = EPOLLIN;

    if(edgeTrigger){
        event.events |= EPOLLET;
    }
    if(resetOneshot){
        event.events |= EPOLLONESHOT;
    }
    if(addEpollout){
        event.events |= EPOLLOUT;
    }

    int ret = epoll_ctl(epollFd, EPOLL_CTL_MOD, modFd, &event);
    if(ret != 0){
        std::cout << outHead("error") << "修改文件描述符失败" << std::endl;
        return -1;
    }
    return 0;
}

int deleteWaitFd(int epollFd, int deleteFd){
    int ret = epoll_ctl(epollFd, EPOLL_CTL_DEL, deleteFd, nullptr);
    if(ret != 0){
        std::cout << outHead("error") << "删除监听的文件描述符失败" << std::endl;
        return -1;
    }
    return 0;
}

int setNonBlocking(int fd){
    int oldFlag = fcntl(fd, F_GETFL);
    int ret = fcntl(fd, F_SETFL, oldFlag | O_NONBLOCK);
    if(ret != 0){
        return -1;
    }
    return 0;
}

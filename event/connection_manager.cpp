#include "connection_manager.h"

#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "epoll_util.h"
#include "../utils/log.h"

std::unordered_map<int, std::shared_ptr<Request> > ConnectionManager::requestStatus;
std::unordered_map<int, std::shared_ptr<Response> > ConnectionManager::responseStatus;
std::unordered_map<int, time_t> ConnectionManager::activeTime;
std::mutex ConnectionManager::activeTimeLocker;
std::mutex ConnectionManager::statusLocker;
std::unordered_map<int, std::shared_ptr<std::mutex> > ConnectionManager::connLockers;

void ConnectionManager::updateActiveTime(int fd){
    std::lock_guard<std::mutex> locker(activeTimeLocker);
    activeTime[fd] = time(nullptr);
}

void ConnectionManager::eraseConnStatus(int fd){
    {
        std::lock_guard<std::mutex> statusGuard(statusLocker);
        requestStatus.erase(fd);
        responseStatus.erase(fd);
        connLockers.erase(fd);
    }
    std::lock_guard<std::mutex> locker(activeTimeLocker);
    activeTime.erase(fd);
}

void ConnectionManager::closeConn(int epollFd, int fd){
    deleteWaitFd(epollFd, fd);
    eraseConnStatus(fd);
    shutdown(fd, SHUT_RDWR);
    close(fd);
}

std::shared_ptr<std::mutex> ConnectionManager::getConnLocker(int fd){
    std::lock_guard<std::mutex> statusGuard(statusLocker);
    auto iter = connLockers.find(fd);
    if(iter != connLockers.end()){
        return iter->second;
    }
    std::shared_ptr<std::mutex> locker(new std::mutex);
    connLockers[fd] = locker;
    return locker;
}

void ConnectionManager::closeExpiredConn(int epollFd, int idleTimeout){
    time_t nowTime = time(nullptr);
    std::vector<int> expiredFdVec;

    {
        std::lock_guard<std::mutex> locker(activeTimeLocker);
        for(const auto &conn : activeTime){
            if(nowTime - conn.second >= idleTimeout){
                expiredFdVec.push_back(conn.first);
            }
        }
    }

    for(const auto &fd : expiredFdVec){
        std::shared_ptr<std::mutex> connLocker = getConnLocker(fd);
        if(!connLocker->try_lock()){
            continue;
        }
        std::lock_guard<std::mutex> connGuard(*connLocker, std::adopt_lock);
        std::cout << outHead("info") << "客户端 " << fd << " 空闲超时，关闭连接" << std::endl;
        closeConn(epollFd, fd);
    }
}

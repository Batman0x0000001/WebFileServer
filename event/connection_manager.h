/*  文件说明：
 *  1. 定义按客户端 fd 管理连接状态的静态工具类。
 *  2. requestStatus 和 responseStatus 保存跨多次 epoll 事件延续的 Request/Response 对象。
 *  3. activeTime 用于空闲连接超时清理，connLockers 保证同一 fd 的读写事件不会并发修改状态。
 *  4. closeConn() 负责从 epoll 删除 fd、清理所有状态并关闭 socket。
 */
#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include <ctime>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "../message/message.h"

class ConnectionManager{
public:
    // 每个连接当前未完成的请求对象。
    static std::unordered_map<int, std::shared_ptr<Request> > requestStatus;
    // 每个连接当前未完成的响应对象。
    static std::unordered_map<int, std::shared_ptr<Response> > responseStatus;
    // 每个连接最后一次活跃时间，用于 SIGALRM 定时清理。
    static std::unordered_map<int, time_t> activeTime;
    static std::mutex activeTimeLocker;
    static std::mutex statusLocker;
    // 每个连接独立一把锁，避免同一 fd 的读写事件并发处理。
    static std::unordered_map<int, std::shared_ptr<std::mutex> > connLockers;

    // 更新 fd 的最后活跃时间。
    static void updateActiveTime(int fd);
    // 清理 fd 对应的请求、响应、连接锁和活跃时间。
    static void eraseConnStatus(int fd);
    // 从 epoll 删除 fd，清理状态并关闭 socket。
    static void closeConn(int epollFd, int fd);
    // 获取 fd 对应的连接锁，不存在时创建。
    static std::shared_ptr<std::mutex> getConnLocker(int fd);
    // 关闭空闲时间超过 idleTimeout 的连接；正在被处理的连接会跳过本轮。
    static void closeExpiredConn(int epollFd, int idleTimeout);
};

#endif

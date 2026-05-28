/*  文件说明：
 *  1. 封装 epoll fd 的添加、修改、删除和非阻塞设置。
 *  2. WebServer 用它注册监听 fd 和信号管道，事件处理器用它重置客户端 fd 的读写监听。
 *  3. 支持边沿触发和 EPOLLONESHOT，避免同一连接事件被多个线程同时处理。
 */
#ifndef EPOLL_UTIL_H
#define EPOLL_UTIL_H

// 向 epollFd 添加文件描述符，并指定是否使用边沿触发和 EPOLLONESHOT。
int addWaitFd(int epollFd, int newFd, bool edgeTrigger = false, bool isOneshot = false);

// 修改已监听 fd 的事件集合；addEpollout 为 true 时切换到可写关注。
int modifyWaitFd(int epollFd, int modFd, bool edgeTrigger = false, bool resetOneshot = false, bool addEpollout = false );

// 从 epoll 中删除文件描述符。
int deleteWaitFd(int epollFd, int deleteFd);

// 设置文件描述符为非阻塞。
int setNonBlocking(int fd);

#endif

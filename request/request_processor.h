/*  文件说明：
 *  1. 定义客户端可读事件的请求处理入口。
 *  2. HandleRecv 准备好连接锁、Request 和 Response 后调用 RequestProcessor::process()。
 *  3. 本类负责非阻塞 recv、HTTP 请求行/头解析、请求体完整性判断和路由调用。
 *  4. 路由处理完成后会切换 epoll 监听为可写事件，让 ResponseSender 继续发送响应。
 */
#ifndef REQUEST_PROCESSOR_H
#define REQUEST_PROCESSOR_H

#include "../message/message.h"

class RequestProcessor{
public:
    // 处理一个 fd 上当前可读的数据，并推进 Request/Response 状态。
    static void process(int clientFd, int epollFd, Request &request, Response &response);
};

#endif

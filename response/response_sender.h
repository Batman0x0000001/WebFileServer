/*  文件说明：
 *  1. 定义客户端可写事件的响应发送入口。
 *  2. HandleSend 取出 Response 后调用 ResponseSender::sendResponse()。
 *  3. 首次发送时会通过 ActionResponseBuilder 根据 ResponseAction 构造具体响应。
 *  4. 支持 JSON、文件和空响应的分段发送；文件响应使用 sendfile 避免把文件读入用户态缓冲区。
 */
#ifndef RESPONSE_SENDER_H
#define RESPONSE_SENDER_H

#include "../message/message.h"

class ResponseSender{
public:
    // 推进指定客户端 fd 的响应发送状态，遇到 EAGAIN 时保留进度等待下次可写事件。
    static void sendResponse(int clientFd, Response &response);
};

#endif

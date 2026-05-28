/*  文件说明：
 *  1. 定义 HTTP 请求和响应在非阻塞事件处理中的状态对象。
 *  2. Request 保存请求行、请求头、未消费缓冲区、请求体长度、keep-alive 和上传临时文件状态。
 *  3. Response 保存路由生成的 ResponseAction、响应头、JSON 响应体、文件 fd 和已发送偏移。
 *  4. ConnectionManager 按 fd 保存 Request/Response，使同一连接可跨多次 epoll 事件继续处理。
 *  5. Request 析构时会删除未完成上传的临时文件；Response 析构时会关闭下载文件 fd。
 */

#ifndef MESSAGE_H
#define MESSAGE_H
#include <iostream>
#include <string>
#include <map>
#include <unordered_map>
#include <utility>
#include <unistd.h>

#include "../router/response_action.h"
#include "../upload/file_body_status.h"

// 表示 Request 或 Response 中数据的处理状态
enum MSGSTATUS{
    HANDLE_INIT,      // 正在接收/发送头部数据（请求行、请求头）
    HANDLE_HEAD,      // 正在接收/发送消息首部
    HANDLE_BODY,      // 正在接收/发送消息体
    HANDLE_COMPLETE,  // 所有数据都已经处理完成
    HANDLE_ERROR,     // 处理过程中发生错误
};

// 表示消息体的类型
enum MSGBODYTYPE{
    FILE_TYPE,      // 消息体是文件
    JSON_TYPE,      // 消息体是 JSON
    EMPTY_TYPE,     // 消息体为空
};

// 表示从客户端接收到的一个 HTTP 请求，保存增量解析和上传处理状态。
class Request{
public:
    Request() : status(HANDLE_INIT), keepAlive(false), msgBodyRecvLen(0), fileMsgStatus(FILE_BEGIN_FLAG){

    }
    ~Request(){
        if(!uploadTempFilePath.empty()){
            unlink(uploadTempFilePath.c_str());
        }
    }

public:
    MSGSTATUS status;                                        // 记录请求报文处理到哪个阶段
    std::unordered_map<std::string, std::string> msgHeader;  // 保存消息首部字段
    std::string recvMsg;           // 收到但是还未处理的数据


    std::string requestMethod;     // 请求消息的请求方法
    std::string requestResourse;    // 请求的资源
    std::string queryString;        // URI 中 ? 后面的 query string
    std::string httpVersion;       // 请求的HTTP版本
    bool keepAlive;                 // 当前请求响应发送完成后是否尝试复用连接

    long long contentLength = 0;                 // 记录消息体的长度
    long long msgBodyRecvLen;                    // 已经接收的消息体长度

    std::string recvFileName;                    // multipart 上传解析出的文件名
    std::string uploadTempFilePath;              // 上传时先写入的临时文件路径
    FILEMSGBODYSTATUS fileMsgStatus;             // multipart 文件体已经处理到哪个阶段
private:

};
// 表示待发送给客户端的 HTTP 响应，保存构造结果和分段发送进度。
class Response{
public:
    Response() : status(HANDLE_INIT), bodyType(EMPTY_TYPE), closeAfterSend(true), beforeBodyMsgLen(0), msgBodyLen(0), fileMsgFd(-1), curStatusHasSendLen(0){

    }
    ~Response(){
        closeFile();
    }

    Response(const Response&) = delete;
    Response& operator=(const Response&) = delete;

    Response(Response &&other) noexcept{
        moveFrom(other);
    }

    Response& operator=(Response &&other) noexcept{
        if(this != &other){
            closeFile();
            moveFrom(other);
        }
        return *this;
    }

    void closeFile(){
        if(fileMsgFd != -1){
            close(fileMsgFd);
            fileMsgFd = -1;
        }
    }

public:
    MSGSTATUS status;                                        // 记录响应报文发送到了多少
    std::unordered_map<std::string, std::string> msgHeader;  // 保存消息首部字段

    // 保存状态行相关数据
    std::string responseHttpVersion = "HTTP/1.1";
    std::string responseStatusCode;  // 如 200、404
    std::string responseStatusDes;   // 如 OK、Not Found

    // 以下成员主要用于在发送响应消息时暂存相关的数据

    MSGBODYTYPE bodyType;                                 // 消息的类型
    ResponseAction action;                                // 路由层传给响应构造层的类型化业务动作
    std::string bodyFileName;                             // 文件响应对应的下载文件名
    std::string username;                                 // 当前响应所属的用户
    bool closeAfterSend;                                  // 当前响应发送完成后是否关闭连接


    std::string beforeBodyMsg;                            // 消息体之前的所有数据
    size_t beforeBodyMsgLen;                              // 消息体之前的所有数据的长度

    std::string msgBody;                                  // 在字符串中保存 JSON 类型的消息体
    unsigned long msgBodyLen;                             // 消息体的长度

    int fileMsgFd;                                        // 文件类型的消息体保存文件描述符

    unsigned long curStatusHasSendLen;                    // 记录在当前状态下，这些数据已经发送的长度
private:
    void moveFrom(Response &other){
        status = other.status;
        msgHeader = std::move(other.msgHeader);
        responseHttpVersion = std::move(other.responseHttpVersion);
        responseStatusCode = std::move(other.responseStatusCode);
        responseStatusDes = std::move(other.responseStatusDes);
        bodyType = other.bodyType;
        action = std::move(other.action);
        bodyFileName = std::move(other.bodyFileName);
        username = std::move(other.username);
        closeAfterSend = other.closeAfterSend;
        beforeBodyMsg = std::move(other.beforeBodyMsg);
        beforeBodyMsgLen = other.beforeBodyMsgLen;
        msgBody = std::move(other.msgBody);
        msgBodyLen = other.msgBodyLen;
        fileMsgFd = other.fileMsgFd;
        curStatusHasSendLen = other.curStatusHasSendLen;
        other.fileMsgFd = -1;
    }
    
};


#endif

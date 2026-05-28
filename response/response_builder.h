/*  文件说明：
 *  1. 定义 HTTP 响应的底层构造工具。
 *  2. ActionResponseBuilder 负责业务动作分发，本类只负责状态行、响应头、JSON 响应体和文件响应头拼装。
 *  3. ResponseSender 首次发送前触发响应构造，之后按 Response 中保存的状态分段发送。
 *  4. 文件响应只构造头部和元数据，真实文件内容由 sendfile 发送，不读入 msgBody。
 */
#ifndef RESPONSE_BUILDER_H
#define RESPONSE_BUILDER_H

#include <string>

#include "../message/message.h"

// 表示服务端常用的 HTTP 响应状态码
enum HTTPSTATUS{
    HTTP_OK = 200,                    // 请求处理成功
    HTTP_BAD_REQUEST = 400,           // 请求参数错误
    HTTP_UNAUTHORIZED = 401,          // 请求没有通过身份认证
    HTTP_METHOD_NOT_ALLOWED = 405,    // 请求方法不被支持
    HTTP_NOT_FOUND = 404,             // 请求的资源不存在
    HTTP_CONFLICT = 409,              // 请求和当前资源状态冲突
    HTTP_PAYLOAD_TOO_LARGE = 413,     // 请求体超过服务器允许的最大长度
    HTTP_NOT_IMPLEMENTED = 501,       // 请求使用了服务端尚未实现的协议能力
    HTTP_INTERNAL_ERROR = 500         // 服务端处理请求失败
};

class ResponseBuilder{
public:
    // 将 HTTP 状态码转换为状态行中的英文描述。
    static std::string getStatusDesc(HTTPSTATUS statusCode);
    // 构建没有响应体的响应，主要用于协议级空响应。
    static void buildEmptyResponse(Response &response, HTTPSTATUS statusCode);
    // 构建 JSON API 响应；extraHeader 用于 Set-Cookie、Allow 等额外头。
    static void buildJsonResponse(Response &response, HTTPSTATUS statusCode, const std::string &msgBody, const std::string &extraHeader = "");
    // 构建文件下载响应头；文件内容后续由 sendfile 分段发送。
    static void buildFileResponseHeader(Response &response, unsigned long fileSize, const std::string &downloadFilename);

private:
    // 写入 Response 对象中的状态字段，并返回 HTTP 状态行字符串。
    static std::string getStatusLine(Response &response, const std::string &httpVersion, const std::string &statusCode, const std::string &statusDesc);
    // 生成通用响应头，按内容类型补充 JSON、下载文件、连接复用等字段。
    static std::string getMessageHeader(const std::string &contentLength, const std::string &contentType, bool closeAfterSend, const std::string &contentRange = "", const std::string &extraHeader = "", const std::string &downloadFilename = "");
};

#endif

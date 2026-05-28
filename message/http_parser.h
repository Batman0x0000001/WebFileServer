/*  文件说明：
 *  1. 提供 HTTP/1.1 请求行和请求头的增量解析能力。
 *  2. RequestProcessor 每次收到新字节后调用本类，把 request.recvMsg 中的可解析部分消费掉。
 *  3. 解析结果只写入 Request 对象，不直接决定业务响应；错误类型由调用方转换为 ResponseAction。
 *  4. 当前只支持 GET/POST、Content-Length 请求体，不支持 chunked 请求体。
 */
#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <string>

#include "message.h"

class HttpRequestParser{
public:
    // 限制请求头大小，防止客户端持续发送头部占用内存。
    static const size_t MAX_HEADER_SIZE = 16 * 1024;

    enum ParseResult{
        PARSE_NEED_MORE,
        PARSE_OK,
        PARSE_BAD_REQUEST,
        PARSE_METHOD_NOT_ALLOWED,
        PARSE_NOT_IMPLEMENTED
    };

    // 当前服务端只实现 GET 和 POST。
    static bool isSupportedMethod(const std::string &method);
    // 校验请求行是否包含方法、资源和 HTTP 版本。
    static bool validateRequestLine(const Request &request);
    // 校验 Content-Length、Transfer-Encoding、Connection 等关键头部。
    static bool validateHeaders(const Request &request);
    // 从 request.recvMsg 中解析一行请求行；数据不完整时返回 PARSE_NEED_MORE。
    static ParseResult parseRequestLine(Request &request);
    // 增量解析请求头；完整头部解析完成后更新 request.status。
    static ParseResult parseHeaders(Request &request);
    // 解析单行请求行和单行 header，供测试和增量解析复用。
    static bool setRequestLine(Request &request, const std::string &requestLine);
    static bool addHeaderOpt(Request &request, const std::string &headLine);
};

#endif

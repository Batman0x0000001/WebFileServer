#include "request_processor.h"

#include <cerrno>
#include <iostream>
#include <sys/socket.h>

#include "../config/config.h"
#include "../event/epoll_util.h"
#include "../message/http_parser.h"
#include "../router/request_router.h"
#include "../utils/log.h"

namespace{

void prepareResponseForSend(int epollFd, int fd, Request &request, Response &response){
    response.closeAfterSend = !request.keepAlive || !request.recvMsg.empty();
    request.status = HANDLE_COMPLETE;
    modifyWaitFd(epollFd, fd, true, true, true);
}

}

void RequestProcessor::process(int clientFd, int epollFd, Request &request, Response &response){
    char buf[2048];
    int recvLen = 0;

    while(1){
        recvLen = recv(clientFd, buf, sizeof(buf), 0);

        if(recvLen == 0){
            std::cout << outHead("info") << "客户端 " << clientFd << " 关闭连接" << std::endl;
            request.status = HANDLE_ERROR;
            break;
        }

        if(recvLen == -1){
            if(errno != EAGAIN){
                request.status = HANDLE_ERROR;
                std::cout << outHead("error") << "接收数据时返回 -1 (errno = " << errno << ")" << std::endl;
                break;
            }
            modifyWaitFd(epollFd, clientFd, true, true, false);
            break;
        }

        request.recvMsg.append(buf, recvLen);
        if((request.status == HANDLE_INIT || request.status == HANDLE_HEAD) &&
           request.recvMsg.size() > HttpRequestParser::MAX_HEADER_SIZE){
            response.action = ResponseAction(ACTION_BAD_REQUEST);
            prepareResponseForSend(epollFd, clientFd, request, response);
            std::cout << outHead("error") << "客户端 " << clientFd << " 请求头超过大小限制" << std::endl;
            break;
        }

        if(request.status == HANDLE_INIT){
            HttpRequestParser::ParseResult parseResult = HttpRequestParser::parseRequestLine(request);
            if(parseResult == HttpRequestParser::PARSE_BAD_REQUEST){
                response.action = ResponseAction(ACTION_BAD_REQUEST);
                prepareResponseForSend(epollFd, clientFd, request, response);
                std::cout << outHead("error") << "客户端 " << clientFd << " 请求行非法" << std::endl;
                break;
            }else if(parseResult == HttpRequestParser::PARSE_METHOD_NOT_ALLOWED){
                response.action = ResponseAction(ACTION_METHOD_NOT_ALLOWED);
                prepareResponseForSend(epollFd, clientFd, request, response);
                std::cout << outHead("error") << "客户端 " << clientFd << " 请求方法不支持" << std::endl;
                break;
            }else if(parseResult == HttpRequestParser::PARSE_OK){
                std::cout << outHead("info") << "处理客户端 " << clientFd << " 的请求行完成" << std::endl;
            }
        }

        if(request.status == HANDLE_HEAD){
            HttpRequestParser::ParseResult parseResult = HttpRequestParser::parseHeaders(request);
            if(parseResult == HttpRequestParser::PARSE_BAD_REQUEST){
                response.action = ResponseAction(ACTION_BAD_REQUEST);
                prepareResponseForSend(epollFd, clientFd, request, response);
                std::cout << outHead("error") << "客户端 " << clientFd << " 请求头非法" << std::endl;
                break;
            }else if(parseResult == HttpRequestParser::PARSE_NOT_IMPLEMENTED){
                response.action = ResponseAction(ACTION_NOT_IMPLEMENTED);
                prepareResponseForSend(epollFd, clientFd, request, response);
                std::cout << outHead("error") << "客户端 " << clientFd << " 使用了尚未支持的 Transfer-Encoding" << std::endl;
                break;
            }else if(parseResult == HttpRequestParser::PARSE_OK){
                if(request.requestMethod == "POST" && request.contentLength > AppConfig::instance().maxUploadSize()){
                    response.action = ResponseAction(ACTION_UPLOAD_TOO_LARGE_JSON);
                    prepareResponseForSend(epollFd, clientFd, request, response);
                    std::cout << outHead("error") << "客户端 " << clientFd << " 上传内容超过大小限制" << std::endl;
                    break;
                }
                std::cout << outHead("info") << "处理客户端 " << clientFd << " 的消息首部完成" << std::endl;
            }
        }

        if(request.status == HANDLE_BODY){
            RouteResult routeResult = routeRequest(request, response, clientFd);
            if(routeResult == ROUTE_HANDLED){
                prepareResponseForSend(epollFd, clientFd, request, response);
                break;
            }else if(routeResult == ROUTE_NEED_MORE_BODY){
                continue;
            }
        }
    }
}

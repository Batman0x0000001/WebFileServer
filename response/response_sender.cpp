#include "response_sender.h"

#include <cerrno>
#include <iostream>
#include <sys/sendfile.h>
#include <sys/socket.h>

#include "action_response_builder.h"
#include "../fileservice/file_service.h"
#include "../utils/log.h"

void ResponseSender::sendResponse(int clientFd, Response &response){
    if(response.status == HANDLE_INIT){
        ActionResponseBuilder::buildFromAction(response);
    }

    while(1){
        long long sentLen = 0;
        if(response.status == HANDLE_HEAD){
            sentLen = response.curStatusHasSendLen;
            sentLen = send(clientFd, response.beforeBodyMsg.c_str() + sentLen, response.beforeBodyMsgLen - sentLen, MSG_NOSIGNAL);
            if(sentLen == -1) {
                if(errno != EAGAIN){
                    response.status = HANDLE_ERROR;
                    std::cout << outHead("error") << "发送响应体和消息首部时返回 -1 (errno = " << errno << ")" << std::endl;
                    break;
                }
                break;
            }
            response.curStatusHasSendLen += sentLen;
            if(response.curStatusHasSendLen >= response.beforeBodyMsgLen){
                response.status = HANDLE_BODY;
                response.curStatusHasSendLen = 0;
                std::cout << outHead("info") << "客户端 " << clientFd << " 响应消息的状态行和消息首部发送完成，正在发送消息体..." << std::endl;
            }

            if(response.bodyType == FILE_TYPE){
                std::cout << outHead("info") << "客户端 " << clientFd << " 请求的是文件，开始发送文件 " << response.bodyFileName << " ..." << std::endl;
            }
        }

        if(response.status == HANDLE_BODY){
            if(response.bodyType == JSON_TYPE){
                sentLen = response.curStatusHasSendLen;
                sentLen = send(clientFd, response.msgBody.c_str() + sentLen, response.msgBodyLen - sentLen, MSG_NOSIGNAL);
                if(sentLen == -1){
                    if(errno != EAGAIN){
                        response.status = HANDLE_ERROR;
                        std::cout << outHead("error") << "发送 JSON 消息体时返回 -1 (errno = " << errno << ")" << std::endl;
                        break;
                    }
                    break;
                }
                response.curStatusHasSendLen += sentLen;

                if(response.curStatusHasSendLen >= response.msgBodyLen){
                    response.status = HANDLE_COMPLETE;
                    response.curStatusHasSendLen = 0;
                    std::cout << outHead("info") << "客户端 " << clientFd << " 请求的是文本响应，发送成功" << std::endl;
                    break;
                }
            }else if(response.bodyType == FILE_TYPE){
                off_t fileOffset = static_cast<off_t>(response.curStatusHasSendLen);
                sentLen = sendfile(clientFd, response.fileMsgFd, &fileOffset, response.msgBodyLen - response.curStatusHasSendLen);
                if(sentLen == -1){
                    if(errno != EAGAIN){
                        response.status = HANDLE_ERROR;
                        std::cout << outHead("error") << "发送文件时返回 -1 (errno = " << errno << ")" << std::endl;
                        break;
                    }
                    break;
                }

                response.curStatusHasSendLen += sentLen;
                if(response.curStatusHasSendLen >= response.msgBodyLen){
                    response.status = HANDLE_COMPLETE;
                    response.curStatusHasSendLen = 0;
                    increaseDownloadCount(response.username, response.bodyFileName);

                    std::cout << outHead("info") << "客户端 " << clientFd << " 请求的文件发送完成" << std::endl;
                    break;
                }
            }else if(response.bodyType == EMPTY_TYPE){
                response.status = HANDLE_COMPLETE;
                response.curStatusHasSendLen = 0;
                std::cout << outHead("info") << "客户端 " << clientFd << " 的空响应发送成功" << std::endl;
                break;
            }
        }

        if(response.status == HANDLE_ERROR){
            break;
        }
    }
}

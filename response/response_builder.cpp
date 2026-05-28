#include "response_builder.h"

#include "../utils/encoding.h"

std::string ResponseBuilder::getStatusLine(Response &response, const std::string &httpVersion, const std::string &statusCode, const std::string &statusDesc){
    response.responseHttpVersion = httpVersion;
    response.responseStatusCode = statusCode;
    response.responseStatusDes = statusDesc;
    return httpVersion + " " + statusCode + " " + statusDesc + "\r\n";
}

std::string ResponseBuilder::getStatusDesc(HTTPSTATUS statusCode){
    if(statusCode == HTTP_OK){
        return "OK";
    }else if(statusCode == HTTP_BAD_REQUEST){
        return "Bad Request";
    }else if(statusCode == HTTP_UNAUTHORIZED){
        return "Unauthorized";
    }else if(statusCode == HTTP_METHOD_NOT_ALLOWED){
        return "Method Not Allowed";
    }else if(statusCode == HTTP_NOT_FOUND){
        return "Not Found";
    }else if(statusCode == HTTP_CONFLICT){
        return "Conflict";
    }else if(statusCode == HTTP_PAYLOAD_TOO_LARGE){
        return "Payload Too Large";
    }else if(statusCode == HTTP_NOT_IMPLEMENTED){
        return "Not Implemented";
    }else if(statusCode == HTTP_INTERNAL_ERROR){
        return "Internal Server Error";
    }
    return "Unknown";
}

void ResponseBuilder::buildEmptyResponse(Response &response, HTTPSTATUS statusCode){
    std::string beforeBodyMsg = getStatusLine(response, "HTTP/1.1", std::to_string(statusCode), getStatusDesc(statusCode));
    beforeBodyMsg += getMessageHeader("0", "", response.closeAfterSend);
    beforeBodyMsg += "\r\n";

    response.beforeBodyMsg = beforeBodyMsg;
    response.beforeBodyMsgLen = response.beforeBodyMsg.size();
    response.bodyType = EMPTY_TYPE;
    response.status = HANDLE_HEAD;
    response.curStatusHasSendLen = 0;
}

void ResponseBuilder::buildJsonResponse(Response &response, HTTPSTATUS statusCode, const std::string &msgBody, const std::string &extraHeader){
    unsigned long msgBodyLen = msgBody.size();
    std::string beforeBodyMsg = getStatusLine(response, "HTTP/1.1", std::to_string(statusCode), getStatusDesc(statusCode));
    beforeBodyMsg += getMessageHeader(std::to_string(msgBodyLen), "json", response.closeAfterSend, "", extraHeader);
    beforeBodyMsg += "\r\n";

    // 发送状态统一重置到响应头，HandleSend 会先发 beforeBodyMsg 再发 JSON body。
    response.msgBody = msgBody;
    response.msgBodyLen = msgBodyLen;
    response.beforeBodyMsg = beforeBodyMsg;
    response.beforeBodyMsgLen = response.beforeBodyMsg.size();
    response.bodyType = JSON_TYPE;
    response.status = HANDLE_HEAD;
    response.curStatusHasSendLen = 0;
}

void ResponseBuilder::buildFileResponseHeader(Response &response, unsigned long fileSize, const std::string &downloadFilename){
    // 当前实现总是返回完整文件，同时保留 Content-Range 方便客户端展示总大小。
    std::string contentRange = "bytes 0-" + std::to_string(fileSize == 0 ? 0 : fileSize - 1) + "/" + std::to_string(fileSize);
    std::string beforeBodyMsg = getStatusLine(response, "HTTP/1.1", std::to_string(HTTP_OK), getStatusDesc(HTTP_OK));
    beforeBodyMsg += getMessageHeader(std::to_string(fileSize), "file", response.closeAfterSend, contentRange, "", downloadFilename);
    beforeBodyMsg += "\r\n";

    response.beforeBodyMsg = beforeBodyMsg;
    response.beforeBodyMsgLen = response.beforeBodyMsg.size();
    response.bodyType = FILE_TYPE;
    response.status = HANDLE_HEAD;
    response.curStatusHasSendLen = 0;
}

std::string ResponseBuilder::getMessageHeader(const std::string &contentLength, const std::string &contentType, bool closeAfterSend, const std::string &contentRange, const std::string &extraHeader, const std::string &downloadFilename){
    std::string headerOpt;

    if(contentLength != ""){
        headerOpt += "Content-Length: " + contentLength + "\r\n";
    }
    if(contentType != ""){
        if(contentType == "json"){
            headerOpt += "Content-Type: application/json;charset=UTF-8\r\n";
        }else if(contentType == "file"){
            headerOpt += "Content-Type: application/octet-stream\r\n";
        }
    }
    if(contentRange != ""){
        headerOpt += "Content-Range: " + contentRange + "\r\n";
    }
    if(downloadFilename != ""){
        headerOpt += "Content-Disposition: attachment; filename=\"" + urlEncode(downloadFilename) + "\"\r\n";
    }
    if(extraHeader != ""){
        headerOpt += extraHeader;
    }
    // 请求体尚有未处理数据或客户端不要求 keep-alive 时，响应后关闭连接。
    headerOpt += closeAfterSend ? "Connection: close\r\n" : "Connection: keep-alive\r\n";
    return headerOpt;
}

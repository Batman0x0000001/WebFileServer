#include "http_parser.h"

#include <cstdlib>
#include <sstream>

#include "../utils/string_utils.h"

namespace{

bool headerContainsToken(const std::string &value, const std::string &expectedToken){
    std::stringstream valueStream(value);
    std::string token;
    std::string expectedLower = toLower(expectedToken);
    while(std::getline(valueStream, token, ',')){
        std::string::size_type begin = token.find_first_not_of(" \t");
        std::string::size_type end = token.find_last_not_of(" \t");
        if(begin == std::string::npos){
            continue;
        }
        if(toLower(token.substr(begin, end - begin + 1)) == expectedLower){
            return true;
        }
    }
    return false;
}

std::string canonicalHeaderKey(const std::string &key){
    std::string lowerKey = toLower(key);
    if(lowerKey == "host"){
        return "Host";
    }
    if(lowerKey == "cookie"){
        return "Cookie";
    }
    if(lowerKey == "accept"){
        return "Accept";
    }
    if(lowerKey == "connection"){
        return "Connection";
    }
    if(lowerKey == "transfer-encoding"){
        return "Transfer-Encoding";
    }
    if(lowerKey == "content-length"){
        return "Content-Length";
    }
    if(lowerKey == "content-type"){
        return "Content-Type";
    }
    return key;
}

}

bool HttpRequestParser::isSupportedMethod(const std::string &method){
    return method == "GET" || method == "POST";
}

bool HttpRequestParser::validateRequestLine(const Request &request){
    return !request.requestMethod.empty() &&
           !request.requestResourse.empty() &&
           request.httpVersion == "HTTP/1.1";
}

bool HttpRequestParser::validateHeaders(const Request &request){
    if(request.httpVersion == "HTTP/1.1"){
        auto hostIter = request.msgHeader.find("Host");
        if(hostIter == request.msgHeader.end() || hostIter->second.empty()){
            return false;
        }
    }
    return true;
}

HttpRequestParser::ParseResult HttpRequestParser::parseRequestLine(Request &request){
    std::string::size_type endIndex = request.recvMsg.find("\r\n");
    if(endIndex == std::string::npos){
        return PARSE_NEED_MORE;
    }

    if(!setRequestLine(request, request.recvMsg.substr(0, endIndex + 2))){
        request.recvMsg.erase(0, endIndex + 2);
        return PARSE_BAD_REQUEST;
    }
    request.recvMsg.erase(0, endIndex + 2);

    if(!validateRequestLine(request)){
        return PARSE_BAD_REQUEST;
    }
    if(!isSupportedMethod(request.requestMethod)){
        return PARSE_METHOD_NOT_ALLOWED;
    }

    request.status = HANDLE_HEAD;
    return PARSE_OK;
}

HttpRequestParser::ParseResult HttpRequestParser::parseHeaders(Request &request){
    while(1){
        std::string::size_type endIndex = request.recvMsg.find("\r\n");
        if(endIndex == std::string::npos){
            return PARSE_NEED_MORE;
        }

        std::string curLine = request.recvMsg.substr(0, endIndex + 2);
        request.recvMsg.erase(0, endIndex + 2);

        if(curLine == "\r\n"){
            if(!validateHeaders(request)){
                return PARSE_BAD_REQUEST;
            }
            auto transferEncodingIter = request.msgHeader.find("Transfer-Encoding");
            if(transferEncodingIter != request.msgHeader.end() &&
               headerContainsToken(transferEncodingIter->second, "chunked")){
                return PARSE_NOT_IMPLEMENTED;
            }
            auto connectionIter = request.msgHeader.find("Connection");
            request.keepAlive = connectionIter != request.msgHeader.end() &&
                                headerContainsToken(connectionIter->second, "keep-alive");
            request.status = HANDLE_BODY;
            if(request.msgHeader["Content-Type"] == "multipart/form-data"){
                request.fileMsgStatus = FILE_BEGIN_FLAG;
            }
            return PARSE_OK;
        }

        if(!addHeaderOpt(request, curLine)){
            return PARSE_BAD_REQUEST;
        }
    }
}

bool HttpRequestParser::setRequestLine(Request &request, const std::string &requestLine){
    std::istringstream lineStream(requestLine);
    std::string requestUri;
    if(!(lineStream >> request.requestMethod >> requestUri >> request.httpVersion)){
        return false;
    }

    std::string::size_type queryIndex = requestUri.find('?');
    if(queryIndex == std::string::npos){
        request.requestResourse = requestUri;
        request.queryString.clear();
    }else{
        request.requestResourse = requestUri.substr(0, queryIndex);
        request.queryString = requestUri.substr(queryIndex + 1);
    }
    return true;
}

bool HttpRequestParser::addHeaderOpt(Request &request, const std::string &headLine){
    if(headLine.empty() || headLine == "\r\n"){
        return true;
    }

    std::string line = headLine;
    if(line.size() >= 2 && line.substr(line.size() - 2) == "\r\n"){
        line.erase(line.size() - 2);
    }

    std::string::size_type colonIndex = line.find(':');
    if(colonIndex == std::string::npos){
        return false;
    }

    std::string key = trim(line.substr(0, colonIndex));
    std::string value = trim(line.substr(colonIndex + 1));
    if(key.empty()){
        return false;
    }

    key = canonicalHeaderKey(key);
    std::string lowerKey = toLower(key);

    if(lowerKey == "content-length"){
        char *end = nullptr;
        long long parsed = std::strtoll(value.c_str(), &end, 10);
        if(value.empty() || end == value.c_str() || *end != '\0' || parsed < 0){
            return false;
        }
        request.contentLength = parsed;
        request.msgHeader[key] = value;
        return true;
    }

    if(lowerKey == "content-type"){
        std::string::size_type semIndex = value.find(';');
        if(semIndex == std::string::npos){
            request.msgHeader[key] = value;
            return true;
        }

        request.msgHeader[key] = trim(value.substr(0, semIndex));
        std::string::size_type paramIndex = semIndex + 1;
        while(paramIndex < value.size()){
            while(paramIndex < value.size() &&
                  (value[paramIndex] == ' ' || value[paramIndex] == '\t' || value[paramIndex] == ';')){
                ++paramIndex;
            }
            if(paramIndex >= value.size()){
                break;
            }

            std::string::size_type nextSemIndex = value.find(';', paramIndex);
            std::string param = value.substr(paramIndex, nextSemIndex - paramIndex);
            std::string::size_type eqIndex = param.find('=');
            if(eqIndex != std::string::npos){
                std::string paramKey = trim(param.substr(0, eqIndex));
                std::string paramValue = trim(param.substr(eqIndex + 1));
                if(paramValue.size() >= 2 && paramValue.front() == '"' && paramValue.back() == '"'){
                    paramValue = paramValue.substr(1, paramValue.size() - 2);
                }
                if(!paramKey.empty()){
                    request.msgHeader[paramKey] = paramValue;
                }
            }
            if(nextSemIndex == std::string::npos){
                break;
            }
            paramIndex = nextSemIndex + 1;
        }
        return true;
    }

    request.msgHeader[key] = value;
    return true;
}

#include "http_helpers.h"

#include "encoding.h"

std::unordered_map<std::string, std::string> parseFormBody(const std::string &body){
    std::unordered_map<std::string, std::string> formData;
    std::string::size_type beginIndex = 0;

    while(beginIndex < body.size()){
        std::string::size_type andIndex = body.find('&', beginIndex);
        std::string item = body.substr(beginIndex, andIndex - beginIndex);
        std::string::size_type eqIndex = item.find('=');
        if(eqIndex != std::string::npos){
            std::string key = urlDecode(item.substr(0, eqIndex));
            std::string value = urlDecode(item.substr(eqIndex + 1));
            formData[key] = value;
        }

        if(andIndex == std::string::npos){
            break;
        }
        beginIndex = andIndex + 1;
    }

    return formData;
}

bool isSafeFilename(const std::string &filename){
    return !filename.empty() &&
           filename != "." &&
           filename != ".." &&
           filename.find('/') == std::string::npos &&
           filename.find('\\') == std::string::npos &&
           filename.find("..") == std::string::npos &&
           filename.find('\0') == std::string::npos;
}

std::string getTokenFromCookie(const std::string &cookie){
    std::string key = "token=";
    std::string::size_type tokenIndex = cookie.find(key);
    if(tokenIndex == std::string::npos){
        return "";
    }

    tokenIndex += key.size();
    std::string::size_type endIndex = cookie.find(';', tokenIndex);
    if(endIndex == std::string::npos){
        return cookie.substr(tokenIndex);
    }
    return cookie.substr(tokenIndex, endIndex - tokenIndex);
}

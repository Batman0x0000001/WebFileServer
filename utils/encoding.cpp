#include "encoding.h"

#include <cctype>
#include <sstream>

std::string urlDecode(const std::string &encoded){
    std::string decoded;
    for(size_t i = 0; i < encoded.size(); ++i){
        if(encoded[i] == '%' && i + 2 < encoded.size()){
            int value;
            std::istringstream iss(encoded.substr(i + 1, 2));
            if(iss >> std::hex >> value){
                decoded += static_cast<char>(value);
                i += 2;
            }else{
                decoded += encoded[i];
            }
        }else if(encoded[i] == '+'){
            decoded += ' ';
        }else{
            decoded += encoded[i];
        }
    }
    return decoded;
}

std::string urlEncode(const std::string &value){
    static const char *hex = "0123456789ABCDEF";
    std::string encoded;
    for(unsigned char ch : value){
        if(std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~'){
            encoded.push_back(static_cast<char>(ch));
        }else{
            encoded.push_back('%');
            encoded.push_back(hex[(ch >> 4) & 0x0f]);
            encoded.push_back(hex[ch & 0x0f]);
        }
    }
    return encoded;
}

std::string htmlEscape(const std::string &value){
    std::string escaped;
    for(const auto &ch : value){
        switch(ch){
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            case '\'': escaped += "&#39;"; break;
            default: escaped += ch; break;
        }
    }
    return escaped;
}

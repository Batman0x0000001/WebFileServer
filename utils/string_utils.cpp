#include "string_utils.h"

#include <algorithm>
#include <cctype>

std::string trim(const std::string &value){
    size_t first = value.find_first_not_of(" \t\r\n");
    if(first == std::string::npos){
        return "";
    }
    size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string toLower(const std::string &value){
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch){
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

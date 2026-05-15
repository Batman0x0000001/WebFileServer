#ifndef ENCODING_H
#define ENCODING_H

#include <string>

std::string urlDecode(const std::string &encoded);
std::string urlEncode(const std::string &value);
std::string htmlEscape(const std::string &value);

#endif

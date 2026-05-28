/*  文件说明：
 *  1. 提供字符串裁剪和大小写转换等通用工具。
 *  2. HTTP 解析、配置解析和表单解析会使用这些函数处理键值前后的空白和大小写。
 *  3. 工具函数不依赖业务模块，避免在解析层引入额外耦合。
 */
#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <string>

// 去掉字符串首尾空白字符。
std::string trim(const std::string &value);
// 将字符串转换为小写，主要用于 HTTP 头字段归一化。
std::string toLower(const std::string &value);

#endif

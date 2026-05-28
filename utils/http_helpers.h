/*  文件说明：
 *  1. 提供 HTTP 业务层常用的小工具。
 *  2. parseFormBody() 解析登录、注册、删除接口的 x-www-form-urlencoded 请求体。
 *  3. isSafeFilename() 统一限制上传、下载、删除中的文件名，防止目录穿越。
 *  4. getTokenFromCookie() 从 Cookie 头中提取登录 session token。
 */
#ifndef HTTP_HELPERS_H
#define HTTP_HELPERS_H

#include <string>
#include <unordered_map>

// 解析 application/x-www-form-urlencoded 请求体。
std::unordered_map<std::string, std::string> parseFormBody(const std::string &body);
// 限制文件名为普通文件名，防止路径穿越和隐藏路径分隔符。
bool isSafeFilename(const std::string &filename);
// 从 Cookie 头中提取 token 字段。
std::string getTokenFromCookie(const std::string &cookie);

#endif

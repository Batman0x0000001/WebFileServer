/*  文件说明：
 *  1. 提供 URL 百分号编码和解码工具。
 *  2. 下载、删除接口使用 urlDecode() 还原请求路径或表单中的文件名。
 *  3. 文件下载响应使用 urlEncode() 生成 Content-Disposition 中的安全文件名。
 */
#ifndef ENCODING_H
#define ENCODING_H

#include <string>

// 解码 URL 百分号编码，用于还原下载和删除接口中的文件名。
std::string urlDecode(const std::string &encoded);
// 编码 URL 中不安全字符，用于 Content-Disposition 文件名。
std::string urlEncode(const std::string &value);

#endif

/*  文件说明：
 *  1. 定义 multipart/form-data 文件上传的增量处理入口和返回状态。
 *  2. request_router.cpp 在识别到 multipart 上传后调用 processMultipartUpload()。
 *  3. 函数会消费 Request::recvMsg，按 boundary 解析文件头和文件内容，并写入临时文件。
 *  4. 上传完整后把临时文件 rename 为正式文件，并按参数决定是否更新 MySQL 文件元信息。
 */
#ifndef MULTIPART_UPLOAD_H
#define MULTIPART_UPLOAD_H

#include <string>

#include "../message/message.h"
#include "file_body_status.h"

enum MultipartUploadResult{
    MULTIPART_NEED_MORE,          // 当前 recvMsg 数据不足，需要继续接收。
    MULTIPART_COMPLETE,           // 文件已完整写入磁盘。
    MULTIPART_INVALID_FILENAME,   // 文件名为空、包含路径或其他不安全字符。
    MULTIPART_FAILED,             // 写文件、创建目录或解析过程发生 I/O 错误。
    MULTIPART_BAD_BOUNDARY        // multipart 边界缺失或格式错误。
};

// 增量解析 multipart/form-data 上传体。
// 该函数会消费 request.recvMsg 中已经处理过的数据，并把文件内容写入用户目录。
MultipartUploadResult processMultipartUpload(Request &request,
                                             const std::string &username,
                                             int clientFd,
                                             bool updateMeta = true);

#endif

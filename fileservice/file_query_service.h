/*  文件说明：
 *  1. 封装文件列表、下载打开、删除元信息这三类面向响应层的文件查询动作。
 *  2. ActionResponseBuilder 通过本文件把业务动作转换成可直接构造 HTTP 响应的数据。
 *  3. 本文件负责 URL 文件名解码、安全文件名校验、未登录和文件不存在等结果分类。
 *  4. 真实文件路径和数据库更新仍委托 file_service.h 和 MysqlClient 完成。
 */
#ifndef FILE_QUERY_SERVICE_H
#define FILE_QUERY_SERVICE_H

#include <string>
#include <vector>

#include "../database/mysqlclient.h"

enum FileQueryStatus{
    FILE_QUERY_OK,                // 查询成功。
    FILE_QUERY_UNAUTHORIZED,      // 缺少登录用户。
    FILE_QUERY_INVALID_FILENAME,  // 文件名不安全或为空。
    FILE_QUERY_NOT_FOUND,         // 下载文件不存在。
    FILE_QUERY_FAILED             // 数据库或文件系统操作失败。
};

struct FileListQueryResult{
    FileListQueryResult() : status(FILE_QUERY_FAILED){
    }

    FileQueryStatus status;
    std::vector<FileMeta> metas;
};

struct DownloadFileQueryResult{
    DownloadFileQueryResult() : status(FILE_QUERY_FAILED), fileFd(-1), fileSize(0){
    }

    FileQueryStatus status;
    int fileFd;
    unsigned long fileSize;
    std::string filename;
};

struct DeleteFileQueryResult{
    DeleteFileQueryResult() : status(FILE_QUERY_FAILED){
    }

    FileQueryStatus status;
};

FileListQueryResult loadUserFileList(const std::string &username);
// 解码并打开指定用户的下载文件，成功时返回文件 fd、大小和解码后的文件名。
DownloadFileQueryResult openUserDownloadFile(const std::string &username, const std::string &encodedFilename);
// 解码并软删除指定用户的文件元信息，不删除磁盘文件。
DeleteFileQueryResult deleteUserFileMeta(const std::string &username, const std::string &encodedFilename);

#endif

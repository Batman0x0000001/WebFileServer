/*  文件说明：
 *  1. 提供文件存储路径、目录创建、文件元信息更新和文件列表缓存失效等基础服务。
 *  2. upload 模块在上传完成后调用 upsertFileMeta()，根据磁盘文件状态写入 MySQL 元信息。
 *  3. response/action_response_builder.cpp 在下载成功或删除文件元信息时通过本文件更新数据库状态。
 *  4. 删除接口只删除 MySQL 元信息并清理缓存，不删除磁盘上的真实文件。
 */
#ifndef FILE_SERVICE_H
#define FILE_SERVICE_H

#include <string>

// 创建文件路径的父目录，支持多级目录。
bool createParentDir(const std::string &filePath);
// 文件列表缓存键，按用户隔离。
std::string getFileListCacheKey(const std::string &username);
// 删除指定用户的文件列表缓存，上传/删除后调用。
void deleteFileListCache(const std::string &username);
// 返回用户专属存储目录。
std::string getUserStorageDir(const std::string &username);
// 根据用户名和安全文件名拼出磁盘路径。
std::string getUserFilePath(const std::string &username, const std::string &filename);
// 扫描磁盘文件状态并写入或更新数据库元信息。
void upsertFileMeta(const std::string &username, const std::string &filename);
// 下载成功后增加数据库下载次数。
void increaseDownloadCount(const std::string &username, const std::string &filename);
// 删除数据库元信息并清理缓存；不删除磁盘文件。
void removeFileMeta(const std::string &username, const std::string &filename);

#endif

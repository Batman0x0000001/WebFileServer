#include "mysqlclient.h"

#include <cstring>
#include <mysql.h>

#include "mysql_statement_utils.h"

bool MysqlClient::upsertFileMeta(const std::string &username, const std::string &filename,
                                 unsigned long fileSize, size_t fileHash, long uploadTime){
    ConnectionGuard guard(*this);
    if(!guard){
        return false;
    }
    MYSQL *conn = static_cast<MYSQL*>(guard.get());
    MYSQL_STMT *stmt = prepareStmt(conn,
        "INSERT INTO files(username,filename,file_size,file_hash,upload_time,download_count) "
        "VALUES(?,?,?,?,FROM_UNIXTIME(?),0) "
        "ON DUPLICATE KEY UPDATE file_size=VALUES(file_size),file_hash=VALUES(file_hash),"
        "upload_time=VALUES(upload_time),is_deleted=0");
    if(stmt == nullptr){
        return false;
    }

    std::string fileHashValue = std::to_string(fileHash);
    unsigned long long fileSizeValue = fileSize;
    long uploadTimeValue = uploadTime;
    MYSQL_BIND params[5];
    unsigned long usernameLen = 0;
    unsigned long filenameLen = 0;
    unsigned long fileHashLen = 0;
    bindStringParam(params[0], username, usernameLen);
    bindStringParam(params[1], filename, filenameLen);
    bindUnsignedLongLongParam(params[2], fileSizeValue);
    bindStringParam(params[3], fileHashValue, fileHashLen);
    bindLongParam(params[4], uploadTimeValue);
    bool ok = executePrepared(stmt, params);
    mysql_stmt_close(stmt);
    return ok;
}

bool MysqlClient::getFileMetas(const std::string &username, std::vector<FileMeta> &metas){
    metas.clear();
    ConnectionGuard guard(*this);
    if(!guard){
        return false;
    }
    MYSQL *conn = static_cast<MYSQL*>(guard.get());
    MYSQL_STMT *stmt = prepareStmt(conn,
        "SELECT filename,file_size,DATE_FORMAT(upload_time,'%Y-%m-%d %H:%i:%s'),download_count "
        "FROM files WHERE username=? AND is_deleted=0 ORDER BY upload_time DESC, filename ASC");
    if(stmt == nullptr){
        return false;
    }

    MYSQL_BIND params[1];
    unsigned long usernameLen = 0;
    bindStringParam(params[0], username, usernameLen);
    bool ok = false;
    if(executePrepared(stmt, params)){
        char filenameBuf[512] = {0};
        char uploadTimeBuf[64] = {0};
        unsigned long filenameLen = 0;
        unsigned long uploadTimeLen = 0;
        unsigned long long fileSize = 0;
        unsigned long long downloadCount = 0;
        MYSQL_BIND result[4];
        std::memset(result, 0, sizeof(result));
        result[0].buffer_type = MYSQL_TYPE_STRING;
        result[0].buffer = filenameBuf;
        result[0].buffer_length = sizeof(filenameBuf) - 1;
        result[0].length = &filenameLen;
        result[1].buffer_type = MYSQL_TYPE_LONGLONG;
        result[1].buffer = &fileSize;
        result[1].is_unsigned = true;
        result[2].buffer_type = MYSQL_TYPE_STRING;
        result[2].buffer = uploadTimeBuf;
        result[2].buffer_length = sizeof(uploadTimeBuf) - 1;
        result[2].length = &uploadTimeLen;
        result[3].buffer_type = MYSQL_TYPE_LONGLONG;
        result[3].buffer = &downloadCount;
        result[3].is_unsigned = true;

        if(mysql_stmt_bind_result(stmt, result) == 0){
            while(mysql_stmt_fetch(stmt) == 0){
                FileMeta meta;
                meta.filename.assign(filenameBuf, filenameLen);
                meta.fileSize = static_cast<unsigned long>(fileSize);
                meta.uploadTime.assign(uploadTimeBuf, uploadTimeLen);
                meta.downloadCount = static_cast<unsigned long>(downloadCount);
                metas.push_back(meta);
            }
            ok = true;
        }
    }
    mysql_stmt_close(stmt);
    return ok;
}

bool MysqlClient::increaseDownloadCount(const std::string &username, const std::string &filename){
    ConnectionGuard guard(*this);
    if(!guard){
        return false;
    }
    MYSQL *conn = static_cast<MYSQL*>(guard.get());
    MYSQL_STMT *stmt = prepareStmt(conn, "UPDATE files SET download_count=download_count+1 WHERE username=? AND filename=? AND is_deleted=0");
    if(stmt == nullptr){
        return false;
    }

    MYSQL_BIND params[2];
    unsigned long usernameLen = 0;
    unsigned long filenameLen = 0;
    bindStringParam(params[0], username, usernameLen);
    bindStringParam(params[1], filename, filenameLen);
    bool ok = executePrepared(stmt, params);
    mysql_stmt_close(stmt);
    return ok;
}

bool MysqlClient::removeFileMeta(const std::string &username, const std::string &filename){
    ConnectionGuard guard(*this);
    if(!guard){
        return false;
    }
    MYSQL *conn = static_cast<MYSQL*>(guard.get());
    MYSQL_STMT *stmt = prepareStmt(conn, "UPDATE files SET is_deleted=1 WHERE username=? AND filename=?");
    if(stmt == nullptr){
        return false;
    }

    MYSQL_BIND params[2];
    unsigned long usernameLen = 0;
    unsigned long filenameLen = 0;
    bindStringParam(params[0], username, usernameLen);
    bindStringParam(params[1], filename, filenameLen);
    bool ok = executePrepared(stmt, params);
    mysql_stmt_close(stmt);
    return ok;
}

#include "file_query_service.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "file_service.h"
#include "../utils/encoding.h"
#include "../utils/http_helpers.h"

FileListQueryResult loadUserFileList(const std::string &username){
    FileListQueryResult result;
    if(username.empty()){
        result.status = FILE_QUERY_UNAUTHORIZED;
        return result;
    }

    if(!MysqlClient::instance().getFileMetas(username, result.metas)){
        result.status = FILE_QUERY_FAILED;
        return result;
    }

    result.status = FILE_QUERY_OK;
    return result;
}

DownloadFileQueryResult openUserDownloadFile(const std::string &username, const std::string &encodedFilename){
    DownloadFileQueryResult result;
    if(username.empty()){
        result.status = FILE_QUERY_UNAUTHORIZED;
        return result;
    }

    std::string decodedFilename = urlDecode(encodedFilename);
    if(!isSafeFilename(decodedFilename)){
        result.status = FILE_QUERY_INVALID_FILENAME;
        return result;
    }

    int fileFd = open(getUserFilePath(username, decodedFilename).c_str(), O_RDONLY);
    if(fileFd == -1){
        result.status = FILE_QUERY_NOT_FOUND;
        return result;
    }

    struct stat fileStat;
    if(fstat(fileFd, &fileStat) != 0){
        close(fileFd);
        result.status = FILE_QUERY_FAILED;
        return result;
    }

    result.status = FILE_QUERY_OK;
    result.fileFd = fileFd;
    result.fileSize = fileStat.st_size;
    result.filename = decodedFilename;
    return result;
}

DeleteFileQueryResult deleteUserFileMeta(const std::string &username, const std::string &encodedFilename){
    DeleteFileQueryResult result;
    if(username.empty()){
        result.status = FILE_QUERY_UNAUTHORIZED;
        return result;
    }

    std::string decodedFilename = urlDecode(encodedFilename);
    if(!isSafeFilename(decodedFilename)){
        result.status = FILE_QUERY_INVALID_FILENAME;
        return result;
    }

    removeFileMeta(username, decodedFilename);
    result.status = FILE_QUERY_OK;
    return result;
}

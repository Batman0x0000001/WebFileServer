#include "file_service.h"

#include <cerrno>
#include <ctime>
#include <functional>
#include <sys/stat.h>

#include "../cache/redisclient.h"
#include "../config/config.h"
#include "../database/mysqlclient.h"

namespace{

bool createDirRecursive(const std::string &dirPath){
    if(dirPath.empty()){
        return true;
    }

    std::string currentPath;
    std::string::size_type beginIndex = 0;
    if(dirPath[0] == '/'){
        currentPath = "/";
        beginIndex = 1;
    }

    while(beginIndex <= dirPath.size()){
        std::string::size_type slashIndex = dirPath.find('/', beginIndex);
        std::string part = dirPath.substr(beginIndex, slashIndex - beginIndex);
        if(!part.empty()){
            if(!currentPath.empty() && currentPath[currentPath.size() - 1] != '/'){
                currentPath += "/";
            }
            currentPath += part;

            if(mkdir(currentPath.c_str(), 0755) != 0 && errno != EEXIST){
                return false;
            }

            struct stat dirStat;
            if(stat(currentPath.c_str(), &dirStat) != 0 || !S_ISDIR(dirStat.st_mode)){
                return false;
            }
        }

        if(slashIndex == std::string::npos){
            break;
        }
        beginIndex = slashIndex + 1;
    }

    return true;
}

}

bool createParentDir(const std::string &filePath){
    std::string::size_type slashIndex = filePath.find_last_of('/');
    if(slashIndex == std::string::npos){
        return true;
    }

    std::string dirPath = filePath.substr(0, slashIndex);
    return createDirRecursive(dirPath);
}

std::string getFileListCacheKey(const std::string &username){
    return "user_files:" + username;
}

void deleteFileListCache(const std::string &username){
    if(username.empty()){
        return;
    }

    RedisClient::instance().del(getFileListCacheKey(username));
}

std::string getUserStorageDir(const std::string &username){
    if(AppConfig::instance().storageDir().empty()){
        return username;
    }
    if(AppConfig::instance().storageDir()[AppConfig::instance().storageDir().size() - 1] == '/'){
        return AppConfig::instance().storageDir() + username;
    }
    return AppConfig::instance().storageDir() + "/" + username;
}

std::string getUserFilePath(const std::string &username, const std::string &filename){
    return getUserStorageDir(username) + "/" + filename;
}

void upsertFileMeta(const std::string &username, const std::string &filename){
    std::string filePath = getUserFilePath(username, filename);

    struct stat fileStat;
    if(stat(filePath.c_str(), &fileStat) != 0){
        return;
    }

    std::hash<std::string> hashFunc;
    size_t fileHash = hashFunc(filePath + std::to_string(fileStat.st_size) + std::to_string(fileStat.st_mtime));
    long uploadTime = time(nullptr);

    MysqlClient::instance().upsertFileMeta(username, filename, fileStat.st_size, fileHash, uploadTime);
    deleteFileListCache(username);
}

void increaseDownloadCount(const std::string &username, const std::string &filename){
    MysqlClient::instance().increaseDownloadCount(username, filename);
}

void removeFileMeta(const std::string &username, const std::string &filename){
    MysqlClient::instance().removeFileMeta(username, filename);
    deleteFileListCache(username);
}

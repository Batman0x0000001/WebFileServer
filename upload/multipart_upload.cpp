#include "multipart_upload.h"

#include <cstdio>
#include <fstream>
#include <unistd.h>

#include "../fileservice/file_service.h"
#include "../utils/http_helpers.h"

namespace{

MultipartUploadResult parseBeginBoundary(Request &request){
    std::string::size_type endIndex = request.recvMsg.find("\r\n");
    if(endIndex == std::string::npos){
        // 边界行还没接收完整，等待下一次 recv。
        return MULTIPART_NEED_MORE;
    }

    std::string flagStr = request.recvMsg.substr(0, endIndex);
    if(flagStr != "--" + request.msgHeader["boundary"]){
        return MULTIPART_BAD_BOUNDARY;
    }

    request.fileMsgStatus = FILE_HEAD;
    request.recvMsg.erase(0, endIndex + 2);
    return MULTIPART_NEED_MORE;
}

MultipartUploadResult parseFileHeader(Request &request){
    while(1){
        std::string::size_type endIndex = request.recvMsg.find("\r\n");
        if(endIndex == std::string::npos){
            // multipart 文件头可能跨 TCP 包，需要保留现有 recvMsg。
            return MULTIPART_NEED_MORE;
        }

        std::string strLine = request.recvMsg.substr(0, endIndex + 2);
        request.recvMsg.erase(0, endIndex + 2);

        if(strLine == "\r\n"){
            // 空行表示 part 头部结束，后续字节都是文件内容。
            request.fileMsgStatus = FILE_CONTENT;
            return MULTIPART_NEED_MORE;
        }

        endIndex = strLine.find("filename");
        if(endIndex != std::string::npos){
            strLine.erase(0, endIndex + std::string("filename=\"").size());
            for(size_t i = 0; i < strLine.size() && strLine[i] != '\"'; ++i){
                request.recvFileName += strLine[i];
            }
        }
    }
}

MultipartUploadResult writeFileContent(Request &request, const std::string &username, int clientFd){
    if(!isSafeFilename(request.recvFileName)){
        return MULTIPART_INVALID_FILENAME;
    }

    std::string uploadFilePath = getUserFilePath(username, request.recvFileName);
    if(request.uploadTempFilePath.empty()){
        // 先写临时文件，完整收到 final boundary 后再替换正式文件。
        request.uploadTempFilePath = uploadFilePath + ".uploading." + std::to_string(clientFd);
        unlink(request.uploadTempFilePath.c_str());
    }
    if(!createParentDir(request.uploadTempFilePath)){
        return MULTIPART_FAILED;
    }

    std::ofstream ofs(request.uploadTempFilePath, std::ios::out | std::ios::app | std::ios::binary);
    if(!ofs){
        return MULTIPART_FAILED;
    }

    size_t consumedLen = 0;
    while(1){
        size_t remainLen = request.recvMsg.size() - consumedLen;
        size_t saveLen = remainLen;
        if(saveLen == 0){
            break;
        }

        std::string::size_type endIndex = request.recvMsg.find('\r', consumedLen);
        if(endIndex != std::string::npos){
            std::string finalBoundary = "\r\n--" + request.msgHeader["boundary"] + "--";
            size_t boundarySecLen = finalBoundary.size();
            if(request.recvMsg.size() - endIndex >= boundarySecLen){
                if(request.recvMsg.substr(endIndex, boundarySecLen) == finalBoundary){
                    // 找到结束边界，边界前的内容写入文件，边界本身从缓冲中消费。
                    if(request.recvMsg.size() - endIndex >= boundarySecLen + 2 &&
                       request.recvMsg.substr(endIndex + boundarySecLen, 2) == "\r\n"){
                        boundarySecLen += 2;
                    }
                    if(endIndex == consumedLen){
                        request.fileMsgStatus = FILE_COMPLETE;
                        consumedLen += boundarySecLen;
                        break;
                    }
                    saveLen = endIndex - consumedLen;
                }else{
                    endIndex = request.recvMsg.find('\r', endIndex + 1);
                    if(endIndex != std::string::npos){
                        saveLen = endIndex - consumedLen;
                    }
                }
            }else{
                if(endIndex == consumedLen){
                    // \r 可能是下一包 final boundary 的开头，暂不写入文件。
                    break;
                }
                saveLen = endIndex - consumedLen;
            }
        }

        ofs.write(request.recvMsg.c_str() + consumedLen, saveLen);
        consumedLen += saveLen;
    }

    if(consumedLen > 0){
        request.recvMsg.erase(0, consumedLen);
    }

    return MULTIPART_NEED_MORE;
}

}

MultipartUploadResult processMultipartUpload(Request &request,
                                             const std::string &username,
                                             int clientFd,
                                             bool updateMeta){
    if(request.fileMsgStatus == FILE_BEGIN_FLAG){
        // 状态机按 multipart 结构依次处理：起始边界 -> part 头 -> 文件内容。
        MultipartUploadResult result = parseBeginBoundary(request);
        if(result != MULTIPART_NEED_MORE || request.fileMsgStatus != FILE_HEAD){
            return result;
        }
    }

    if(request.fileMsgStatus == FILE_HEAD){
        MultipartUploadResult result = parseFileHeader(request);
        if(result != MULTIPART_NEED_MORE || request.fileMsgStatus != FILE_CONTENT){
            return result;
        }
    }

    if(request.fileMsgStatus == FILE_CONTENT){
        MultipartUploadResult result = writeFileContent(request, username, clientFd);
        if(result != MULTIPART_NEED_MORE){
            return result;
        }
    }

    if(request.fileMsgStatus == FILE_COMPLETE){
        std::string uploadFilePath = getUserFilePath(username, request.recvFileName);
        if(rename(request.uploadTempFilePath.c_str(), uploadFilePath.c_str()) != 0){
            return MULTIPART_FAILED;
        }
        request.uploadTempFilePath.clear();
        if(updateMeta){
            // 文件落盘成功后再更新数据库，避免列表中出现不存在的文件。
            upsertFileMeta(username, request.recvFileName);
        }
        return MULTIPART_COMPLETE;
    }

    return MULTIPART_NEED_MORE;
}

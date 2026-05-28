#include <cassert>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "../upload/multipart_upload.h"
#include "../fileservice/file_service.h"

namespace{

std::string readFile(const std::string &path){
    std::ifstream file(path, std::ios::in | std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void cleanupUserFile(const std::string &username, const std::string &filename){
    std::string filePath = getUserFilePath(username, filename);
    unlink(filePath.c_str());
    std::string tempPath = filePath + ".uploading.99999";
    unlink(tempPath.c_str());
    rmdir(getUserStorageDir(username).c_str());
}

}

int main(){
    const std::string username = "multipart_test_user";
    const std::string filename = "note.txt";
    cleanupUserFile(username, filename);

    Request request;
    request.msgHeader["boundary"] = "----boundary";
    request.recvMsg =
        "------boundary\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"note.txt\"\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "hello\r\nworld"
        "\r\n------boundary--\r\n";

    MultipartUploadResult result = processMultipartUpload(request, username, 99999, false);
    assert(result == MULTIPART_COMPLETE);
    assert(request.recvFileName == filename);
    assert(request.uploadTempFilePath.empty());
    assert(readFile(getUserFilePath(username, filename)) == "hello\r\nworld");
    cleanupUserFile(username, filename);

    Request partial;
    partial.msgHeader["boundary"] = "----boundary";
    partial.recvMsg = "------boundary";
    assert(processMultipartUpload(partial, username, 99999, false) == MULTIPART_NEED_MORE);

    Request invalid;
    invalid.msgHeader["boundary"] = "----boundary";
    invalid.recvMsg =
        "------boundary\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"../bad.txt\"\r\n"
        "\r\n"
        "body\r\n------boundary--\r\n";
    assert(processMultipartUpload(invalid, username, 99999, false) == MULTIPART_INVALID_FILENAME);

    return 0;
}

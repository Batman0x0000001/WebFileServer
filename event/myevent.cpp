#include <string>
#include <sstream>
#include <vector>
#include <functional>
#include <mutex>
#include <cerrno>
#include <fcntl.h>
#include "myevent.h"
#include "../auth/auth.h"
#include "../config/config.h"
#include "../database/mysqlclient.h"
#include "../cache/redisclient.h"
#include "../utils/encoding.h"

namespace{

std::mutex dirReadLocker;

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

// 如果文件路径包含目录，先逐级创建父目录
bool createParentDir(const std::string &filePath){
    std::string::size_type slashIndex = filePath.find_last_of('/');
    if(slashIndex == std::string::npos){
        return true;
    }

    std::string dirPath = filePath.substr(0, slashIndex);
    return createDirRecursive(dirPath);
}

// 读取本地 HTML 模板，读取失败时返回简单错误页面
std::string loadHtmlFile(const std::string &filePath){
    std::ifstream htmlFile(filePath);
    if(!htmlFile){
        return "<html><body><h1>500 Internal Server Error</h1><p>HTML template not found.</p></body></html>\n";
    }

    std::ostringstream htmlStream;
    htmlStream << htmlFile.rdbuf();
    return htmlStream.str();
}

// 解析 application/x-www-form-urlencoded 格式的请求体
std::unordered_map<std::string, std::string> parseFormBody(const std::string &body){
    std::unordered_map<std::string, std::string> formData;
    std::string::size_type beginIndex = 0;

    while(beginIndex < body.size()){
        std::string::size_type andIndex = body.find('&', beginIndex);
        std::string item = body.substr(beginIndex, andIndex - beginIndex);
        std::string::size_type eqIndex = item.find('=');
        if(eqIndex != std::string::npos){
            std::string key = urlDecode(item.substr(0, eqIndex));
            std::string value = urlDecode(item.substr(eqIndex + 1));
            formData[key] = value;
        }

        if(andIndex == std::string::npos){
            break;
        }
        beginIndex = andIndex + 1;
    }

    return formData;
}

// 文件名最终会拼接到用户目录下，只允许单层文件名，避免目录穿越。
bool isSafeFilename(const std::string &filename){
    return !filename.empty() &&
           filename != "." &&
           filename != ".." &&
           filename.find('/') == std::string::npos &&
           filename.find('\\') == std::string::npos &&
           filename.find("..") == std::string::npos &&
           filename.find('\0') == std::string::npos;
}

// 用户文件列表缓存按用户隔离，避免不同用户之间互相看到文件名
std::string getFileListCacheKey(const std::string &username){
    return "user_files:" + username;
}

void deleteFileListCache(const std::string &username){
    if(username.empty()){
        return;
    }

    RedisClient::instance().del(getFileListCacheKey(username));
}

// 从 Cookie 头中提取 token=xxx
std::string getTokenFromCookie(const std::string &cookie){
    std::string key = "token=";
    std::string::size_type tokenIndex = cookie.find(key);
    if(tokenIndex == std::string::npos){
        return "";
    }

    tokenIndex += key.size();
    std::string::size_type endIndex = cookie.find(';', tokenIndex);
    if(endIndex == std::string::npos){
        return cookie.substr(tokenIndex);
    }
    return cookie.substr(tokenIndex, endIndex - tokenIndex);
}

// 根据 Cookie 中的 token 获取当前登录用户
bool getLoginUserFromCookie(const std::string &cookie, std::string &username){
    std::string token = getTokenFromCookie(cookie);
    return getUserByToken(token, username);
}

// 获取用户自己的文件服务目录，所有上传、下载、删除都限制在该目录下。
std::string getUserStorageDir(const std::string &username){
    if(AppConfig::instance().storageDir().empty()){
        return username;
    }
    if(AppConfig::instance().storageDir()[AppConfig::instance().storageDir().size() - 1] == '/'){
        return AppConfig::instance().storageDir() + username;
    }
    return AppConfig::instance().storageDir() + "/" + username;
}

// 获取用户目录下的文件路径
std::string getUserFilePath(const std::string &username, const std::string &filename){
    return getUserStorageDir(username) + "/" + filename;
}

// 上传完成后写入或更新文件元数据，并清理对应用户的文件列表缓存。
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

// 下载成功时更新 MySQL 中的下载次数。
void increaseDownloadCount(const std::string &username, const std::string &filename){
    MysqlClient::instance().increaseDownloadCount(username, filename);
}

// 删除文件时删除对应元数据，并清理对应用户的文件列表缓存。
void removeFileMeta(const std::string &username, const std::string &filename){
    MysqlClient::instance().removeFileMeta(username, filename);
    deleteFileListCache(username);
}

}

// 类外初始化静态成员
std::unordered_map<int, Request> EventBase::requestStatus;
std::unordered_map<int, Response> EventBase::responseStatus;
std::unordered_map<int, time_t> EventBase::activeTime;
std::mutex EventBase::activeTimeLocker;
std::mutex EventBase::statusLocker;
std::unordered_map<int, std::shared_ptr<std::mutex> > EventBase::connLockers;

// 更新连接的最后活跃时间
void EventBase::updateActiveTime(int fd){
    std::lock_guard<std::mutex> locker(activeTimeLocker);
    activeTime[fd] = time(nullptr);
}

// 删除连接相关的请求、响应和活跃时间状态
void EventBase::eraseConnStatus(int fd){
    {
        std::lock_guard<std::mutex> statusGuard(statusLocker);
        requestStatus.erase(fd);
        responseStatus.erase(fd);
        connLockers.erase(fd);
    }
    std::lock_guard<std::mutex> locker(activeTimeLocker);
    activeTime.erase(fd);
}

// 关闭一个超时或异常连接
void EventBase::closeConn(int epollFd, int fd){
    deleteWaitFd(epollFd, fd);
    eraseConnStatus(fd);
    shutdown(fd, SHUT_RDWR);
    close(fd);
}

std::shared_ptr<std::mutex> EventBase::getConnLocker(int fd){
    std::lock_guard<std::mutex> statusGuard(statusLocker);
    auto iter = connLockers.find(fd);
    if(iter != connLockers.end()){
        return iter->second;
    }
    std::shared_ptr<std::mutex> locker(new std::mutex);
    connLockers[fd] = locker;
    return locker;
}

// 清理超过空闲时间的连接
void EventBase::closeExpiredConn(int epollFd, int idleTimeout){
    time_t nowTime = time(nullptr);
    std::vector<int> expiredFdVec;

    // 先找出所有超时连接，再逐个关闭，避免遍历 map 时删除元素
    {
        std::lock_guard<std::mutex> locker(activeTimeLocker);
        for(const auto &conn : activeTime){
            if(nowTime - conn.second >= idleTimeout){
                expiredFdVec.push_back(conn.first);
            }
        }
    }

    for(const auto &fd : expiredFdVec){
        std::shared_ptr<std::mutex> connLocker = getConnLocker(fd);
        if(!connLocker->try_lock()){
            continue;
        }
        std::lock_guard<std::mutex> connGuard(*connLocker, std::adopt_lock);
        std::cout << outHead("info") << "客户端 " << fd << " 空闲超时，关闭连接" << std::endl;
        closeConn(epollFd, fd);
    }
}

// 用于接受客户端连接的事件
void AcceptConn::process(){
    while(1){
        sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        int accFd = accept(m_listenFd, (sockaddr*)&clientAddr, &clientAddrLen);
        if(accFd == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                return;
            }
            std::cout << outHead("error") << "接受新连接失败" << std::endl;
            return;
        }

        setNonBlocking(accFd);
        addWaitFd(m_epollFd, accFd, true, true);
        updateActiveTime(accFd);
        std::cout << outHead("info") << "接受新连接 " << accFd << " 成功" << std::endl;
    }
}

// 处理信号事件
void HandleSig::process(){
    int sigBuf[32];
    int readLen = 0;

    // 读取信号管道中的所有信号，避免管道中残留旧信号
    while((readLen = recv(m_pipeFd, sigBuf, sizeof(sigBuf), 0)) > 0){
        int sigNum = readLen / sizeof(int);
        for(int i = 0; i < sigNum; ++i){
            if(sigBuf[i] == SIGALRM){
                closeExpiredConn(m_epollFd, AppConfig::instance().idleTimeout());
                alarm(AppConfig::instance().timerInterval());
                std::cout << outHead("info") << "定时器事件处理完成" << std::endl;
            }
        }
    }
}

// 处理客户端发送的请求
void HandleRecv::process(){
    // 同一个连接的请求可能分多次 epoll 可读事件到达。
    // 这里取出 fd 对应的 Request/Response 状态，随后按状态机继续解析。
    std::shared_ptr<std::mutex> connLocker = getConnLocker(m_clientFd);
    std::lock_guard<std::mutex> connGuard(*connLocker);
    Request *requestPtr = nullptr;
    Response *responsePtr = nullptr;
    {
        std::lock_guard<std::mutex> statusGuard(statusLocker);
        requestPtr = &requestStatus[m_clientFd];
        responsePtr = &responseStatus[m_clientFd];
    }
    Request &request = *requestPtr;
    Response &response = *responsePtr;
    std::cout << outHead("info") << "开始处理客户端 " << m_clientFd << " 的一个 HandleRecv 事件" << std::endl;
    updateActiveTime(m_clientFd);
    // 获取 Request 对象，保存到m_clientFd索引的requestStatus中（没有时会自动创建一个新的）

    // 读取输入，检测是否是断开连接，否则处理请求
    char buf[2048];
    int recvLen = 0;
    
    while(1){
        // 循环接收数据，直到缓冲区读取不到数据或请求消息处理完成时退出循环
        recvLen = recv(m_clientFd, buf, sizeof(buf), 0);

        // 对方关闭连接，直接断开连接，设置当前状态为 HANDLE_ERROR，再退出循环
        if(recvLen == 0){
            std::cout << outHead("info") << "客户端 " << m_clientFd << " 关闭连接" << std::endl;
            request.status = HANDLE_ERROR;
            break;
        }

        //如果缓冲区的数据已经读完，退出读数据的状态
        if(recvLen == -1){
            if(errno != EAGAIN){    // 如果不是缓冲区为空，设置状态为错误，并退出循环
                request.status = HANDLE_ERROR;
                std::cout << outHead("error") << "接收数据时返回 -1 (errno = " << errno << ")" << std::endl;
                break;
            }
            // 如果是缓冲区为空，表示需要等待数据发送，由于是 EPOLLONESHOT，再退出循环，等再发来数据时再来处理
            modifyWaitFd(m_epollFd, m_clientFd, true, true, false);
            break;
        }

        // 将收到的数据拼接到之前收到的数据后面，由于在处理文件时，里面可能有 \0，所以使用 append 将 buf 内的所有字符都保存到 recvMsg 中
        request.recvMsg.append(buf, recvLen);

        // 边接收数据边处理
        // 根据请求报文的状态执行操作，以下操作中，如果成功了，则解析请求报文的下个部分，如果某个部分还没有完全接收，会退出当前处理步骤，等再次收到数据后根据这次解析的状态继续处理
        
        // 保存字符串查找结果，每次查找都可以用该变量暂存查找结果
        std::string::size_type endIndex = 0;
        
        // 如果是初始状态，获取请求行
        // POST /upload HTTP/1.1\r\n,setRequestLine 会解析出 requestMethod="POST"，requestResourse="/upload"，httpVersion="HTTP/1.1"。
        if(request.status == HANDLE_INIT){

            endIndex = request.recvMsg.find("\r\n");       // 查找请求行的结束边界

            if(endIndex != std::string::npos){
                // 保存请求行  
                request.setRequestLine(request.recvMsg.substr(0, endIndex + 2) ); // std::cout << request.recvMsg.substr(0, endIndex + 2);
                request.recvMsg.erase(0, endIndex + 2);    // 删除收到的数据中的请求行
                request.status = HANDLE_HEAD;              // 将状态设置为处理消息首部
                std::cout << outHead("info") << "处理客户端 " << m_clientFd << " 的请求行完成" << std::endl;
            }

            // 如果没有找到 \r\n，表示数据还没有接收完成，会跳回上面继续接收数据
        }
        
        // 如果是处理首部的状态，逐行解析首部字段，直至遇到空行
        //请求头可能包含 Content-Type: multipart/form-data; boundary=----WebKitFormBoundaryxxx，addHeaderOpt 会解析出 Content-Type 和 boundary（用于后续文件边界判断）。
        if(request.status == HANDLE_HEAD){
            
            std::string curLine;       // 用于暂存获取的一行数据

            while(1){
                
                endIndex = request.recvMsg.find("\r\n");            // 获取一行的边界
                if(endIndex == std::string::npos){                                    // 如果没有找到边界，表示后面的数据还没有接收完整，退出循环，等待下次接收后处理
                    break;
                }

                curLine = request.recvMsg.substr(0, endIndex + 2);  // 将该行的内容取出
                request.recvMsg.erase(0, endIndex + 2);             // 删除收到的数据中的该行数据

                if(curLine == "\r\n"){
                    request.status = HANDLE_BODY;                                       // 如果是空行，将状态修改为等待解析消息体
                    if(request.msgHeader["Content-Type"] == "multipart/form-data"){     // 如果接收的是文件，设置消息体中文件的处理状态
                        request.fileMsgStatus = FILE_BEGIN_FLAG;
                    }
                    if(request.requestMethod == "POST" && request.contentLength > AppConfig::instance().maxUploadSize()){
                        response.action = ResponseAction(ACTION_UPLOAD_TOO_LARGE);
                        modifyWaitFd(m_epollFd, m_clientFd, true, true, true);
                        request.status = HANDLE_COMPLETE;
                        std::cout << outHead("error") << "客户端 " << m_clientFd << " 上传内容超过大小限制" << std::endl;
                    }
                    std::cout << outHead("info") << "处理客户端 " << m_clientFd << " 的消息首部完成" << std::endl;
                    if(request.requestMethod == "POST"){
                        std::cout << outHead("info") << "客户端 " << m_clientFd << " 发送 POST 请求，开始处理请求体" << std::endl;
                    }
                    break;                                                                                // 退出首部字段循环
                }
                
                request.addHeaderOpt(curLine);                      // 如果不是空行，需要将该首部保存
            }
        }

        // 如果是处理消息体的状态，根据请求类型执行特定的操作
        if(request.status == HANDLE_BODY){
            // GET 操作时表示请求数据，将请求的资源路径交给 HandleSend 事件处理
            if(request.requestMethod == "GET"){
                if(request.requestResourse == "/login"){
                    response.action = ResponseAction(ACTION_LOGIN_PAGE);
                    modifyWaitFd(m_epollFd, m_clientFd, true, true, true);
                    request.status = HANDLE_COMPLETE;
                    std::cout << outHead("info") << "客户端 " << m_clientFd << " 请求登录页面" << std::endl;
                    break;
                }
                if(request.requestResourse == "/api/me"){    // 获取当前登录用户
                    std::string username;
                    std::string token = getTokenFromCookie(request.msgHeader["Cookie"]);
                    if(getUserByToken(token, username)){
                        response.action = ResponseAction(ACTION_ME_SUCCESS, username);
                    }else{
                        response.action = ResponseAction(ACTION_ME_UNAUTHORIZED);
                    }
                    modifyWaitFd(m_epollFd, m_clientFd, true, true, true);
                    request.status = HANDLE_COMPLETE;
                    std::cout << outHead("info") << "客户端 " << m_clientFd << " 请求当前登录用户信息" << std::endl;
                    break;
                }
                if(request.requestResourse == "/api/logout"){    // 登出接口，删除当前 token
                    std::string token = getTokenFromCookie(request.msgHeader["Cookie"]);
                    deleteSessionToken(token);
                    response.action = ResponseAction(ACTION_LOGOUT_SUCCESS);
                    modifyWaitFd(m_epollFd, m_clientFd, true, true, true);
                    request.status = HANDLE_COMPLETE;
                    std::cout << outHead("info") << "客户端 " << m_clientFd << " 请求登出" << std::endl;
                    break;
                }

                // 设置响应消息的资源路径，在 HandleSend 中根据请求资源构建整个响应消息并发送
                if(!getLoginUserFromCookie(request.msgHeader["Cookie"], response.username)){
                    response.action = ResponseAction(ACTION_LOGIN_PAGE);
                    std::cout << outHead("error") << "客户端 " << m_clientFd << " 未登录，拒绝访问文件资源" << std::endl;
                }else if(request.requestResourse == "/"){
                    response.action = ResponseAction(ACTION_FILE_LIST);
                }else if(request.requestResourse.find("/download/") == 0){
                    response.action = ResponseAction(ACTION_DOWNLOAD, request.requestResourse.substr(std::string("/download/").size()));
                }else{
                    response.action = ResponseAction(ACTION_NOT_FOUND);
                }

                // 设置监听套接字的可写事件，当套接字写缓冲区有空闲数据时，会产生 HandleSend 事件，将 m_clientFd 索引的 responseStatus 中的数据发送
                modifyWaitFd(m_epollFd, m_clientFd, true, true, true);
                request.status = HANDLE_COMPLETE;
                std::cout << outHead("info") << "客户端 " << m_clientFd << " 发送 GET 请求，已将请求资源构成 Response 写事件等待发送数据" << std::endl; 
                break;
            }

            // POST 表示上传数据，执行接收数据的操作
            if(request.requestMethod == "POST"){
                if(request.requestResourse == "/api/register"){   // 注册接口，处理表单格式的用户名和密码
                    if(request.recvMsg.size() < static_cast<size_t>(request.contentLength)){
                        break;
                    }

                    std::string body = request.recvMsg.substr(0, request.contentLength);
                    request.recvMsg.erase(0, request.contentLength);
                    auto formData = parseFormBody(body);
                    std::string username = formData["username"];
                    std::string password = formData["password"];

                    if(!isValidUsername(username) || password.size() < 6){
                        response.action = ResponseAction(ACTION_REGISTER_INVALID);
                        std::cout << outHead("error") << "客户端 " << m_clientFd << " 注册参数非法" << std::endl;
                    }else if(!saveUser(username, password)){
                        response.action = ResponseAction(ACTION_REGISTER_DUPLICATE);
                        std::cout << outHead("error") << "客户端 " << m_clientFd << " 注册用户 " << username << " 失败，可能是用户名重复" << std::endl;
                    }else{
                        response.action = ResponseAction(ACTION_REGISTER_SUCCESS);
                        std::cout << outHead("info") << "客户端 " << m_clientFd << " 注册用户 " << username << " 成功" << std::endl;
                    }

                    modifyWaitFd(m_epollFd, m_clientFd, true, true, true);
                    request.status = HANDLE_COMPLETE;
                    break;
                }else if(request.requestResourse == "/api/login"){    // 登录接口，校验用户并生成 token
                    if(request.recvMsg.size() < static_cast<size_t>(request.contentLength)){
                        break;
                    }

                    std::string body = request.recvMsg.substr(0, request.contentLength);
                    request.recvMsg.erase(0, request.contentLength);
                    auto formData = parseFormBody(body);
                    std::string username = formData["username"];
                    std::string password = formData["password"];

                    if(checkUserPassword(username, password)){
                        std::string token = createSessionToken(username);
                        response.action = ResponseAction(ACTION_LOGIN_SUCCESS, token);
                        std::cout << outHead("info") << "客户端 " << m_clientFd << " 登录用户 " << username << " 成功" << std::endl;
                    }else{
                        response.action = ResponseAction(ACTION_LOGIN_UNAUTHORIZED);
                        std::cout << outHead("error") << "客户端 " << m_clientFd << " 登录失败" << std::endl;
                    }

                    modifyWaitFd(m_epollFd, m_clientFd, true, true, true);
                    request.status = HANDLE_COMPLETE;
                    break;
                }else if(request.requestResourse == "/api/delete"){
                    if(!getLoginUserFromCookie(request.msgHeader["Cookie"], response.username)){
                        response.action = ResponseAction(ACTION_ME_UNAUTHORIZED);
                        modifyWaitFd(m_epollFd, m_clientFd, true, true, true);
                        request.status = HANDLE_COMPLETE;
                        std::cout << outHead("error") << "客户端 " << m_clientFd << " 未登录，拒绝删除文件" << std::endl;
                        break;
                    }
                    if(request.recvMsg.size() < static_cast<size_t>(request.contentLength)){
                        break;
                    }

                    std::string body = request.recvMsg.substr(0, request.contentLength);
                    request.recvMsg.erase(0, request.contentLength);
                    auto formData = parseFormBody(body);
                    response.action = ResponseAction(ACTION_DELETE, formData["filename"]);
                    modifyWaitFd(m_epollFd, m_clientFd, true, true, true);
                    request.status = HANDLE_COMPLETE;
                    break;
                }else if(request.msgHeader["Content-Type"] == "multipart/form-data"){  // 如果发送的是文件
                    if(!getLoginUserFromCookie(request.msgHeader["Cookie"], response.username)){
                        response.action = ResponseAction(ACTION_ME_UNAUTHORIZED);
                        modifyWaitFd(m_epollFd, m_clientFd, true, true, true);
                        request.status = HANDLE_COMPLETE;
                        std::cout << outHead("error") << "客户端 " << m_clientFd << " 未登录，拒绝上传文件" << std::endl;
                        break;
                    }

                    // 如果处于等待处理文件开始标志的状态，查找 \r\n 判断标志部分是否已经接收
                    if(request.fileMsgStatus == FILE_BEGIN_FLAG){
                        std::cout << outHead("info") << "客户端 " << m_clientFd << " 的 POST 请求用于上传文件，寻找文件头开始边界..." << std::endl;
                        // 查找 \r\n
                        endIndex = request.recvMsg.find("\r\n");

                        // 当前状态下，\r\n 前的数据必然是文件信息开始的标志
                        if(endIndex != std::string::npos){
                            std::string flagStr = request.recvMsg.substr(0, endIndex);

                            if(flagStr == "--" +request.msgHeader["boundary"]){  // 如果等于 "--" 加边界，进入下一个状态
                                request.fileMsgStatus = FILE_HEAD;               // 进入下一个状态
                                request.recvMsg.erase(0, endIndex + 2);          // 将开始标志行删除（包括 /r/n）
                                std::cout << outHead("info") << "客户端 " << m_clientFd << " 的 POST 请求体中找到文件头开始边界，正在处理文件头..." << std::endl;
                            }else{
                                // 如果和边界不同，表示出错，直接返回重定向报文，重新请求文件列表
                                response.action = ResponseAction(ACTION_REDIRECT);
                                modifyWaitFd(m_epollFd, m_clientFd, true, true, true);   // 重置可读事件和可写事件，用于发送重定向回复报文
                                request.status = HANDLE_COMPLETE;
                                std::cout << outHead("error") << "客户端 " << m_clientFd << " 的 POST 请求体中没有找到文件头开始边界，添加重定向 Response 写事件，使客户端重定向到文件列表" << std::endl;
                                break;
                            }
                        }
                    }

                    // 如果处于等待接收并处理消息体中文件头部信息的状态，从中提取文件名
                    if(request.fileMsgStatus == FILE_HEAD){
                        std::string strLine;
                        while(1){
                            // 查找 \r\n 表示一行数据
                            endIndex = request.recvMsg.find("\r\n");
                            if(endIndex != std::string::npos){
                                strLine = request.recvMsg.substr(0, endIndex + 2);  // 获取这一行的数据信息
                                request.recvMsg.erase(0, endIndex + 2);             // 删除这一行信息

                                // 检测是否为空行，如果是空行，修改状态，退出
                                if(strLine == "\r\n"){
                                    request.fileMsgStatus = FILE_CONTENT;
                                    std::cout << outHead("info") << "客户端 " << m_clientFd << " 的 POST 请求体中文件头处理成功，正在接收并保存文件内容..." << std::endl;
                                    break;
                                }
                                // 查找 strLine 是否包含 filename
                                endIndex = strLine.find("filename");
                                if(endIndex != std::string::npos){
                                    strLine.erase(0, endIndex + std::string("filename=\"").size());          // 将真正 filename 前的所有字符删除
                                    for(int i = 0; strLine[i] != '\"'; ++i){                                 // 保存文件名
                                        request.recvFileName += strLine[i];
                                    }
                                    std::cout << outHead("info") << "客户端 " << m_clientFd << " 的 POST 请求体中找到文件名字 " << request.recvFileName << " ，继续处理文件头..." << std::endl; 
                                }
                            }else{   // 如果没有找到，表示消息还没有接收完整，退出，等待下一轮的事件中继续处理
                                break;
                            }
                        }
                    }

                    // 如果处于等待并处消息体中文件内容部分
                    // 循环检索是否有 \r\n ，将 \r\n 之前的内容全部保存。如果存在\r\n，根据后面的内容判断是否到达文件边界
                    if(request.fileMsgStatus == FILE_CONTENT){
                        if(!isSafeFilename(request.recvFileName)){
                            response.action = ResponseAction(ACTION_UPLOAD_INVALID);
                            modifyWaitFd(m_epollFd, m_clientFd, true, true, true);
                            request.status = HANDLE_COMPLETE;
                            std::cout << outHead("error") << "客户端 " << m_clientFd << " 上传文件名非法" << std::endl;
                            break;
                        }
                        std::string uploadFilePath = getUserFilePath(response.username, request.recvFileName);
                        if(!createParentDir(uploadFilePath)){
                            response.action = ResponseAction(ACTION_UPLOAD_FAILED);
                            modifyWaitFd(m_epollFd, m_clientFd, true, true, true);
                            request.status = HANDLE_COMPLETE;
                            std::cout << outHead("error") << "客户端 " << m_clientFd << " 上传目录创建失败" << std::endl;
                            break;
                        }
                        // 首先以二进制追加的方式打开文件
                        std::ofstream ofs(uploadFilePath, std::ios::out | std::ios::app | std::ios::binary);
                        if(!ofs){
                            std::cout << outHead("error") << "客户端 " << m_clientFd << " 的 POST 请求体所需要保存的文件打开失败，正在重新打开文件..." << std::endl;
                            response.action = ResponseAction(ACTION_UPLOAD_FAILED);
                            modifyWaitFd(m_epollFd, m_clientFd, true, true, true);
                            request.status = HANDLE_COMPLETE;
                            break;
                        }

                        size_t consumedLen = 0;
                        while(1){
                            size_t remainLen = request.recvMsg.size() - consumedLen;
                            size_t saveLen = remainLen;        // 该变量用来保存 根据\r的位置决定向文件中写入多少字符，初始为所有字符长度
                            if(saveLen == 0){                                              // 长度为空时退出循环，等待接收到数据时再处理
                                break;
                            }
                            // 在剩余的字符中搜索标志 \r
                            endIndex = request.recvMsg.find('\r', consumedLen);
                                        
                            if(endIndex != std::string::npos){   // 如果有\r，后面有可能是文件结束标识
                                // 首先判断 \r 后的数据是否满足结束标识的长度，是否大于等于 sizeof(\r\n + "--" + boundary + "--" + \r\n)
                                size_t boundarySecLen = request.msgHeader["boundary"].size() + 8;
                                if(request.recvMsg.size() - endIndex >= boundarySecLen){
                                    // 判断后面这部分数据是否为结束边界"\r\n"
                                    if(request.recvMsg.substr(endIndex, boundarySecLen) ==
                                                    "\r\n--" + request.msgHeader["boundary"] + "--\r\n"){
                                        if(endIndex == consumedLen){                  // 表示边界前的数据都已经写入文件，设置文件接收完成，进入下一个状态
                                            std::cout << outHead("info") << "客户端 " << m_clientFd << " 的 POST 请求体中的文件数据接收并保存完成" << std::endl;
                                            request.fileMsgStatus = FILE_COMPLETE;
                                            consumedLen += boundarySecLen;
                                            break;
                                        }

                                        // 如果后面不是结束标识，先将 \r 之前的所有数据写入文件，在循环的下一轮会进到上一个if，结束整个处理
                                        saveLen = endIndex - consumedLen;
                                        
                                    }else{  
                                        // 如果不是边界，在 \r 后再次搜索 \r，如果搜索到了，写入的数据截至到第二个 \r，否则将所有数据写入
                                        endIndex = request.recvMsg.find('\r', endIndex + 1);
                                        if(endIndex != std::string::npos){
                                            saveLen = endIndex - consumedLen;
                                        }
                                    }

                                }else{  
                                    // 如果长度还不够，将 /r 前面的数据写入文件，并等待接收后面的数据

                                    // 如果 /r 之前的数据已经写入文件，退出循环，等待接收更多数据后再进入该循环
                                    if(endIndex == consumedLen){   
                                        break;
                                    }

                                    // 否则将 endIndex 前的数据写入文件
                                    saveLen = endIndex - consumedLen;
                                }
                            }
                            // 如果没有退出表示当前仍是数据部分，将 saveLen 字节的数据存入文件，并将这些数据从 recvMsg 数据中删除
                            ofs.write(request.recvMsg.c_str() + consumedLen, saveLen);
                            consumedLen += saveLen;
                        }
                        if(consumedLen > 0){
                            request.recvMsg.erase(0, consumedLen);
                        }
                        ofs.close();
                    }
                    // std::cout << "已退出文件接收函数" << std::endl;
                    // 如果文件已经处理完成，设置消息体为完成状态
                    if(request.fileMsgStatus == FILE_COMPLETE){
                        upsertFileMeta(response.username, request.recvFileName);
                        // 设置响应消息的资源路径，在 HandleSend 中根据请求资源构建整个响应消息并发送
                        response.action = ResponseAction(ACTION_REDIRECT);
                        modifyWaitFd(m_epollFd, m_clientFd, true, true, true);   // 完成后重置可读事件和可写事件，用于发送重定向回复报文
                        request.status = HANDLE_COMPLETE;
                        std::cout << outHead("info") << "客户端 " << m_clientFd << " 的 POST 请求体处理完成，添加 Response 写事件，发送重定向报文刷新文件列表" << std::endl;
                        break;
                    }
                }else{    // POST 是其他类型的数据
                    // 其他 POST 类型的数据时，直接返回重定向报文，获取文件列表
                    response.action = ResponseAction(ACTION_REDIRECT);
                    modifyWaitFd(m_epollFd, m_clientFd, true, true, true);
                    request.status = HANDLE_COMPLETE;
                    std::cout << outHead("error") << "客户端 " << m_clientFd << " 的 POST 请求中接收到不能处理的数据，添加 Response 写事件，返回重定向到文件列表的报文" << std::endl;
                    break;
                }
            }

        }

    }

    
    if(request.status == HANDLE_COMPLETE){     // 如果请求处理完成，将该套接字对应的请求删除
        std::cout << outHead("info") << "客户端 " << m_clientFd << " 的请求消息处理成功" << std::endl;
        {
            std::lock_guard<std::mutex> statusGuard(statusLocker);
            requestStatus.erase(m_clientFd);
        }
    }else if(request.status == HANDLE_ERROR){        
        // 请求处理错误，关闭该文件描述符，将该套接字对应的请求删除，从监听列表中删除该文件描述符
        std::cout << outHead("error") << "客户端 " << m_clientFd << " 的请求消息处理失败，关闭连接" << std::endl;
        closeConn(m_epollFd, m_clientFd);
    }
    
}

// 处理向客户端发送数据
void HandleSend::process(){
    // 响应可能因为 socket 写缓冲区满而分多次发送。
    // responseStatus 记录本 fd 当前响应已经构建和发送到哪里。
    std::shared_ptr<std::mutex> connLocker = getConnLocker(m_clientFd);
    std::lock_guard<std::mutex> connGuard(*connLocker);
    Response *responsePtr = nullptr;
    {
        std::lock_guard<std::mutex> statusGuard(statusLocker);
        auto responseIter = responseStatus.find(m_clientFd);
        if(responseIter == responseStatus.end()){
            std::cout << outHead("info") << "客户端 " << m_clientFd << " 没有要处理的响应消息" << std::endl;
            return;
        }
        responsePtr = &responseIter->second;
    }
    Response &response = *responsePtr;
    std::cout << outHead("info") << "开始处理客户端 " << m_clientFd << " 的一个 HandleSend 事件" << std::endl;
    updateActiveTime(m_clientFd);
    // 根据 Response 对象的状态执行特定的处理

    // 如果处于初始状态，根据请求的文件构建不同类型的发送数据
    if(response.status == HANDLE_INIT){
        if(response.action.type == ACTION_FILE_LIST){
            if(response.username.empty()){
                buildHtmlResponse(response, HTTP_OK, loadHtmlFile("html/login.html"));
                std::cout << outHead("info") << "客户端 " << m_clientFd << " 未登录，不能查看文件列表" << std::endl;
            }else{
                std::string fileListPage;
                getFileListPage(fileListPage, response.username);
                buildHtmlResponse(response, HTTP_OK, fileListPage);
                std::cout << outHead("info") << "客户端 " << m_clientFd << " 的响应消息用来返回文件列表页面，状态行和消息体已构建完成" << std::endl;
            }
        }else if(response.action.type == ACTION_LOGIN_PAGE){
            buildHtmlResponse(response, HTTP_OK, loadHtmlFile("html/login.html"));
            std::cout << outHead("info") << "客户端 " << m_clientFd << " 的登录页面已构建完成" << std::endl;
        }else if(response.action.type == ACTION_DOWNLOAD){
            if(response.username.empty()){
                buildHtmlResponse(response, HTTP_UNAUTHORIZED, "<html><body><h1>401 Unauthorized</h1><p>Please login first.</p></body></html>\n");
                std::cout << outHead("info") << "客户端 " << m_clientFd << " 未登录，不能下载文件" << std::endl;
            }else{
                std::string decodedFilename = urlDecode(response.action.value);
                if(!isSafeFilename(decodedFilename)){
                    buildHtmlResponse(response, HTTP_BAD_REQUEST, "<html><body><h1>400 Bad Request</h1><p>Invalid filename.</p></body></html>\n");
                    std::cout << outHead("error") << "客户端 " << m_clientFd << " 下载文件名非法" << std::endl;
                }else{
                    response.fileMsgFd = open(getUserFilePath(response.username, decodedFilename).c_str(), O_RDONLY);
                    if(response.fileMsgFd == -1){
                        std::cout << outHead("error") << "客户端 " << m_clientFd << " 的请求消息要下载文件 " << decodedFilename << " ，但是文件打开失败，返回 404 响应" << std::endl;
                        buildHtmlResponse(response, HTTP_NOT_FOUND, "<html><body><h1>404 Not Found</h1><p>File not found.</p></body></html>\n");
                    }else{
                        response.bodyFileName = decodedFilename;
                        struct stat fileStat;
                        fstat(response.fileMsgFd, &fileStat);
                        response.msgBodyLen = fileStat.st_size;
                        buildFileResponseHeader(response, response.msgBodyLen, decodedFilename);
                        std::cout << outHead("info") << "客户端 " << m_clientFd << " 的请求消息要下载文件 " << decodedFilename << " ，文件打开成功，根据文件构建响应消息状态行和头部信息成功" << std::endl;
                    }
                }
            }
        }else if(response.action.type == ACTION_DELETE){
            if(response.username.empty()){
                buildHtmlResponse(response, HTTP_UNAUTHORIZED, "<html><body><h1>401 Unauthorized</h1><p>Please login first.</p></body></html>\n");
                std::cout << outHead("info") << "客户端 " << m_clientFd << " 未登录，不能删除文件" << std::endl;
            }else{
                std::string decodedFilename = urlDecode(response.action.value);
                if(!isSafeFilename(decodedFilename)){
                    buildHtmlResponse(response, HTTP_BAD_REQUEST, "<html><body><h1>400 Bad Request</h1><p>Invalid filename.</p></body></html>\n");
                    std::cout << outHead("error") << "客户端 " << m_clientFd << " 删除文件名非法" << std::endl;
                }else{
                    int ret = remove(getUserFilePath(response.username, decodedFilename).c_str());
                    if(ret != 0){
                        std::cout << outHead("error") << "客户端 " << m_clientFd << " 的请求消息要删除文件 " << decodedFilename << " 但是文件删除失败" << std::endl;
                    }else{
                        removeFileMeta(response.username, decodedFilename);
                        std::cout << outHead("info") << "客户端 " << m_clientFd << " 的请求消息要删除文件 " << decodedFilename << " 且文件删除成功" << std::endl;
                    }

                    buildEmptyResponse(response, HTTP_FOUND, "/");
                }
            }
        }else if(response.action.type == ACTION_REDIRECT){
            buildEmptyResponse(response, HTTP_FOUND, "/");
            std::cout << outHead("info") << "客户端 " << m_clientFd << " 的响应报文是重定向报文，状态行和消息首部已构建完成" << std::endl;
        }else if(response.action.type == ACTION_REGISTER_SUCCESS){
            buildHtmlResponse(response, HTTP_OK, "<html><body><h1>Register Success</h1><p><a href=\"/login\">Go Login</a></p></body></html>\n");
            std::cout << outHead("info") << "客户端 " << m_clientFd << " 的注册成功响应已构建完成" << std::endl;
        }else if(response.action.type == ACTION_REGISTER_INVALID){
            buildHtmlResponse(response, HTTP_BAD_REQUEST, "<html><body><h1>400 Bad Request</h1><p>Invalid username or password.</p></body></html>\n");
            std::cout << outHead("info") << "客户端 " << m_clientFd << " 的注册参数错误响应已构建完成" << std::endl;
        }else if(response.action.type == ACTION_REGISTER_DUPLICATE){
            buildHtmlResponse(response, HTTP_CONFLICT, "<html><body><h1>409 Conflict</h1><p>Username already exists.</p></body></html>\n");
            std::cout << outHead("info") << "客户端 " << m_clientFd << " 的注册冲突响应已构建完成" << std::endl;
        }else if(response.action.type == ACTION_UPLOAD_INVALID){
            buildHtmlResponse(response, HTTP_BAD_REQUEST, "<html><body><h1>400 Bad Request</h1><p>Invalid filename.</p></body></html>\n");
            std::cout << outHead("info") << "客户端 " << m_clientFd << " 的上传文件名错误响应已构建完成" << std::endl;
        }else if(response.action.type == ACTION_UPLOAD_FAILED){
            buildHtmlResponse(response, HTTP_INTERNAL_ERROR, "<html><body><h1>500 Internal Server Error</h1><p>Upload failed.</p></body></html>\n");
            std::cout << outHead("info") << "客户端 " << m_clientFd << " 的上传失败响应已构建完成" << std::endl;
        }else if(response.action.type == ACTION_UPLOAD_TOO_LARGE){
            buildHtmlResponse(response, HTTP_PAYLOAD_TOO_LARGE, "<html><body><h1>413 Payload Too Large</h1><p>Upload file is too large.</p></body></html>\n");
        }else if(response.action.type == ACTION_LOGIN_SUCCESS){
            buildHtmlResponse(response, HTTP_OK, "<html><head><meta http-equiv=\"refresh\" content=\"0; url=/\"></head><body><h1>Login Success</h1><p>Redirecting...</p></body></html>\n", "Set-Cookie: token=" + response.action.value + "; Path=/\r\n");
            std::cout << outHead("info") << "客户端 " << m_clientFd << " 的登录成功响应已构建完成" << std::endl;
        }else if(response.action.type == ACTION_LOGIN_UNAUTHORIZED){
            buildHtmlResponse(response, HTTP_UNAUTHORIZED, "<html><body><h1>401 Unauthorized</h1><p>Invalid username or password.</p></body></html>\n");
            std::cout << outHead("info") << "客户端 " << m_clientFd << " 的登录失败响应已构建完成" << std::endl;
        }else if(response.action.type == ACTION_ME_SUCCESS){
            buildHtmlResponse(response, HTTP_OK, "<html><body><h1>Current User</h1><p>" + htmlEscape(response.action.value) + "</p></body></html>\n");
            std::cout << outHead("info") << "客户端 " << m_clientFd << " 的当前用户响应已构建完成" << std::endl;
        }else if(response.action.type == ACTION_ME_UNAUTHORIZED){
            buildHtmlResponse(response, HTTP_UNAUTHORIZED, "<html><body><h1>401 Unauthorized</h1><p>Please login first.</p></body></html>\n");
            std::cout << outHead("info") << "客户端 " << m_clientFd << " 的未登录响应已构建完成" << std::endl;
        }else if(response.action.type == ACTION_LOGOUT_SUCCESS){
            buildHtmlResponse(response, HTTP_OK, "<html><body><h1>Logout Success</h1></body></html>\n", "Set-Cookie: token=; Path=/; Max-Age=0\r\n");
            std::cout << outHead("info") << "客户端 " << m_clientFd << " 的登出响应已构建完成" << std::endl;
        }else{
            buildHtmlResponse(response, HTTP_NOT_FOUND, "<html><body><h1>404 Not Found</h1><p>Request resource not found.</p></body></html>\n");
            std::cout << outHead("info") << "客户端 " << m_clientFd << " 的请求资源不存在，已构建 404 响应报文" << std::endl;
        }
    }

    while(1){
        long long sentLen = 0;
        // 发送响应消息头
        if(response.status == HANDLE_HEAD){
            // 开始发送消息体之前的所有数据
            sentLen = response.curStatusHasSendLen;
            sentLen = send(m_clientFd, response.beforeBodyMsg.c_str() + sentLen, response.beforeBodyMsgLen - sentLen, MSG_NOSIGNAL);
            if(sentLen == -1) {
                if(errno != EAGAIN){
                    // 如果不是缓冲区满，设置发送失败状态，并退出循环
                    response.status = HANDLE_ERROR;
                    std::cout << outHead("error") << "发送响应体和消息首部时返回 -1 (errno = " << errno << ")" << std::endl;
                    break;
                }
                // 如果缓冲区已满，退出循环，下面会重置 EPOLLOUT 事件，等待下次进入函数继续发送
                break;
            }
            response.curStatusHasSendLen += sentLen;
            // 如果数据已经发送完成，将状态设置为发送消息体
            if(response.curStatusHasSendLen >= response.beforeBodyMsgLen){
                response.status = HANDLE_BODY;     // 设置为正在处理消息体的状态
                response.curStatusHasSendLen = 0;   // 设置已经发送的数据长度为 0
                std::cout << outHead("info") << "客户端 " << m_clientFd << " 响应消息的状态行和消息首部发送完成，正在发送消息体..." << std::endl;
            }

            // 如果发送的是文件，输出提示信息
            if(response.bodyType == FILE_TYPE){
                std::cout << outHead("info") << "客户端 " << m_clientFd << " 请求的是文件，开始发送文件 " << response.bodyFileName << " ..." << std::endl;
            }
        }

        // 发送响应消息体
        if(response.status == HANDLE_BODY){
            // 根据发送数据的类型执行特定的发送操作
            if(response.bodyType == HTML_TYPE){
                // 消息体为 HTML 页面时的发送方法
                sentLen = response.curStatusHasSendLen;
                sentLen = send(m_clientFd, response.msgBody.c_str() + sentLen, response.msgBodyLen - sentLen, MSG_NOSIGNAL);
                if(sentLen == -1){
                    if(errno != EAGAIN){
                        // 如果不是缓冲区满，设置发送失败状态，并退出循环
                        response.status = HANDLE_ERROR;
                        std::cout << outHead("error") << "发送 HTML 消息体时返回 -1 (errno = " << errno << ")" << std::endl;
                        break;
                    }
                    
                    // 如果缓冲区已满，退出循环，下面会重置 EPOLLOUT 事件，等待下次进入函数继续发送
                    break;
                }
                response.curStatusHasSendLen += sentLen;
                
                // 如果数据已经发送完成，将状态设置为发送消息体
                if(response.curStatusHasSendLen >= response.msgBodyLen){
                    response.status = HANDLE_COMPLETE;     // 设置为正在处理消息体的状态
                    response.curStatusHasSendLen = 0;   // 设置已经发送的数据长度为 0
                    std::cout << outHead("info") << "客户端 " << m_clientFd << " 请求的是 HTML 文件，文件发送成功" << std::endl;
                    break;
                }

            }else if(response.bodyType == FILE_TYPE){
                // 消息体是文件时的发送方法
                
                // 获取已经发送的字节数，用来控制下面函数从哪个地方开始发送
                off_t fileOffset = static_cast<off_t>(response.curStatusHasSendLen);
                
                // 使用 sendfile 函数，实现零拷贝的发送数据，提高效率
                sentLen = sendfile(m_clientFd, response.fileMsgFd, &fileOffset, response.msgBodyLen - response.curStatusHasSendLen);
                if(sentLen == -1){
                    if(errno != EAGAIN){
                        // 如果不是缓冲区满，设置发送失败状态
                        response.status = HANDLE_ERROR;
                        std::cout << outHead("error") << "发送文件时返回 -1 (errno = " << errno << ")" << std::endl;
                        break;
                    }
                    // 如果缓冲区已满，退出循环，下面会重置 EPOLLOUT 事件，等待下次进入函数继续发送
                    break;
                }
                
                // 累加已发送的数据长度
                response.curStatusHasSendLen += sentLen;

                // 文件发送完成后，重置 Response 为访问根目录的响应，向客户端传递文件列表
                if(response.curStatusHasSendLen >= response.msgBodyLen){
                    response.status = HANDLE_COMPLETE;     // 设置为事件处理完成
                    response.curStatusHasSendLen = 0;       // 设置已经发送的数据长度为 0
                    increaseDownloadCount(response.username, response.bodyFileName);

                    std::cout << outHead("info") << "客户端 " << m_clientFd << " 请求的文件发送完成" << std::endl;
                    break;
                }

            }else if(response.bodyType == EMPTY_TYPE){
                // 消息体为空时直接进入下个状态，目前用于重定向报文的消息体发送
                response.status = HANDLE_COMPLETE;       // 设置为事件处理完成
                response.curStatusHasSendLen = 0;         // 设置已经发送的数据长度为 0
                std::cout << outHead("info") << "客户端 " << m_clientFd << " 的重定向报文发送成功" << std::endl;
                break;
            }
        }

        if(response.status == HANDLE_ERROR){    // 如果是出错状态，退出 while 处理
            break;
        }
    }
    

    // 判断发送最终状态执行特定的操作
    if(response.status == HANDLE_COMPLETE){
        // 完成发送数据后删除该响应
        {
            std::lock_guard<std::mutex> statusGuard(statusLocker);
            responseStatus.erase(m_clientFd);
        }
        modifyWaitFd(m_epollFd, m_clientFd, true, true, false);                            // 不再监听写事件
        std::cout << outHead("info") << "客户端 " << m_clientFd << " 的响应报文发送成功" << std::endl;
        return;
    }else if(response.status == HANDLE_ERROR){
        // 如果发送失败，删除该响应，删除监听该文件描述符，关闭连接
        closeConn(m_epollFd, m_clientFd);
        std::cout << outHead("error") << "客户端 " << m_clientFd << " 的响应报文发送失败，关闭相关的文件描述符" << std::endl;
        return;
    }else{                      // 如果不是完成了数据传输或出错，应该重置 EPOLLSHOT 事件，保证写事件可以继续产生，继续传输数据
        modifyWaitFd(m_epollFd, m_clientFd, true, true, true);

        // 退出函数，当执行失败时或数据传输完成时才需要关闭文件
        return;
    }

}

// 用于构建状态行，参数分别表示状态行的三个部分
std::string HandleSend::getStatusLine(Response &response, const std::string &httpVersion, const std::string &statusCode, const std::string &statusDes){
    std::string statusLine;
    response.responseHttpVersion = httpVersion;
    response.responseStatusCode = statusCode;
    response.responseStatusDes = statusDes;
    // 构建状态行
    statusLine = httpVersion + " ";
    statusLine += statusCode + " ";
    statusLine += statusDes + "\r\n";

    return statusLine;
}

// 根据状态码获取对应的状态描述
std::string HandleSend::getStatusDesc(HTTPSTATUS statusCode){
    if(statusCode == HTTP_OK){
        return "OK";
    }else if(statusCode == HTTP_FOUND){
        return "Found";
    }else if(statusCode == HTTP_BAD_REQUEST){
        return "Bad Request";
    }else if(statusCode == HTTP_UNAUTHORIZED){
        return "Unauthorized";
    }else if(statusCode == HTTP_NOT_FOUND){
        return "Not Found";
    }else if(statusCode == HTTP_CONFLICT){
        return "Conflict";
    }else if(statusCode == HTTP_PAYLOAD_TOO_LARGE){
        return "Payload Too Large";
    }else if(statusCode == HTTP_INTERNAL_ERROR){
        return "Internal Server Error";
    }
    return "Unknown";
}

// 构建空消息体响应，目前主要用于重定向
void HandleSend::buildEmptyResponse(Response &response, HTTPSTATUS statusCode, const std::string &redirectLoction){
    std::string beforeBodyMsg = getStatusLine(response, "HTTP/1.1", std::to_string(statusCode), getStatusDesc(statusCode));
    beforeBodyMsg += getMessageHeader("0", "html", redirectLoction, "");
    beforeBodyMsg += "\r\n";

    response.beforeBodyMsg = beforeBodyMsg;
    response.beforeBodyMsgLen = response.beforeBodyMsg.size();
    response.bodyType = EMPTY_TYPE;
    response.status = HANDLE_HEAD;
    response.curStatusHasSendLen = 0;
}

// 构建 HTML 消息体响应，用于正常页面和错误页面
void HandleSend::buildHtmlResponse(Response &response, HTTPSTATUS statusCode, const std::string &msgBody, const std::string &extraHeader){
    unsigned long msgBodyLen = msgBody.size();
    std::string beforeBodyMsg = getStatusLine(response, "HTTP/1.1", std::to_string(statusCode), getStatusDesc(statusCode));
    beforeBodyMsg += getMessageHeader(std::to_string(msgBodyLen), "html", "", "", extraHeader);
    beforeBodyMsg += "\r\n";

    response.msgBody = msgBody;
    response.msgBodyLen = msgBodyLen;
    response.beforeBodyMsg = beforeBodyMsg;
    response.beforeBodyMsgLen = response.beforeBodyMsg.size();
    response.bodyType = HTML_TYPE;
    response.status = HANDLE_HEAD;
    response.curStatusHasSendLen = 0;
}

// 构建文件响应头，文件内容仍由 sendfile 发送
void HandleSend::buildFileResponseHeader(Response &response, unsigned long fileSize, const std::string &downloadFilename){
    std::string contentRange = "bytes 0-" + std::to_string(fileSize == 0 ? 0 : fileSize - 1) + "/" + std::to_string(fileSize);
    std::string beforeBodyMsg = getStatusLine(response, "HTTP/1.1", std::to_string(HTTP_OK), getStatusDesc(HTTP_OK));
    beforeBodyMsg += getMessageHeader(std::to_string(fileSize), "file", "", contentRange, "", downloadFilename);
    beforeBodyMsg += "\r\n";

    response.beforeBodyMsg = beforeBodyMsg;
    response.beforeBodyMsgLen = response.beforeBodyMsg.size();
    response.bodyType = FILE_TYPE;
    response.status = HANDLE_HEAD;
    response.curStatusHasSendLen = 0;
}

// 以下两个函数用来构建文件列表的页面，最终结果保存到 fileListHtml 中
void HandleSend::getFileListPage(std::string &fileListHtml, const std::string &username){
    // 结果保存到 fileListHtml
    std::string cacheValue;
    if(RedisClient::instance().get(getFileListCacheKey(username), cacheValue)){
        fileListHtml = cacheValue;
        std::cout << outHead("info") << "用户 " << username << " 的文件列表命中 Redis 缓存" << std::endl;
        return;
    }

    // 将指定目录内的所有文件保存到 fileVec 中
    std::vector<std::string> fileVec;
    getFileVec(getUserStorageDir(username), fileVec);
    
    // 构建页面
    std::ifstream fileListStream("html/filelist.html", std::ios::in);
    if(!fileListStream){
        fileListHtml = "<html><body><h1>500 Internal Server Error</h1><p>File list template not found.</p></body></html>\n";
        return;
    }
    std::string tempLine;
    // 首先读取文件列表的 <!--filelist_label--> 注释前的语句
    while(getline(fileListStream, tempLine)){
        if(tempLine == "<!--filelist_label-->"){
            break;
        }
        fileListHtml += tempLine + "\n";
    }
    if(tempLine != "<!--filelist_label-->"){
        fileListHtml = "<html><body><h1>500 Internal Server Error</h1><p>File list template marker not found.</p></body></html>\n";
        return;
    }

    // 根据模板标签，将文件夹中的所有文件项添加到返回页面中
    for(const auto &filename : fileVec){
        std::string escapedFilename = htmlEscape(filename);
        std::string encodedFilename = urlEncode(filename);
        fileListHtml += "            <tr><td class=\"col1\">" + escapedFilename +
                    "</td> <td class=\"col2\"><a href=\"download/" + encodedFilename +
                    "\">下载</a></td> <td class=\"col3\"><form action=\"/api/delete\" method=\"post\" onsubmit=\"return confirmDelete();\" style=\"margin:0;\">"
                    "<input type=\"hidden\" name=\"filename\" value=\"" + encodedFilename +
                    "\"><button type=\"submit\">删除</button></form></td></tr>" + "\n";
    }

    // 将文件列表注释后的语句加入后面
    while(getline(fileListStream, tempLine)){
        fileListHtml += tempLine + "\n";
    }

    RedisClient::instance().setex(getFileListCacheKey(username),
                                  AppConfig::instance().fileListCacheTtl(),
                                  fileListHtml);
    
}
/**
 * @brief 获取指定目录下的所有文件名并存储在结果向量中
 *
 * 使用 dirent 库获取指定目录下的所有文件名，并将其存储在传入的结果向量中。
 *
 * @param dirName 指定目录的路径
 * @param resVec 用于存储获取到的文件名的结果向量
 */
void HandleSend::getFileVec(const std::string &dirName, std::vector<std::string> &resVec){
    // 使用 dirent 获取文件目录下的所有文件
    std::lock_guard<std::mutex> locker(dirReadLocker);
    DIR *dir;   // 目录指针
    dir = opendir(dirName.c_str());
    if(dir == nullptr){
        std::cout << outHead("error") << "打开文件服务目录失败：" << dirName << std::endl;
        return;
    }
    struct dirent *stdinfo;
    while (1)
    {
        // 获取文件夹中的一个文件
        stdinfo = readdir(dir);
        if (stdinfo == nullptr){
            break;
        }
        resVec.push_back(stdinfo->d_name);
        if(resVec.back() == "." || resVec.back() == ".."){
            resVec.pop_back();
        }
    }
    closedir(dir);
}

// 构建头部字段：
// contentLength        : 指定消息体的长度
// contentType          : 指定消息体的类型
// redirectLoction = "" : 如果是重定向报文，可以指定重定向的地址。空字符串表示不添加该首部。
// contentRange = ""    : 如果是下载文件的响应报文，指定当前发送的文件范围。空字符串表示不添加该首部。
/**
 * @brief 生成HTTP响应头字符串
 *
 * 根据输入的内容长度、内容类型、重定向位置和内容范围生成HTTP响应头字符串。
 *
 * @param contentLength 内容长度字符串
 * @param contentType 内容类型字符串
 * @param redirectLoction 重定向位置字符串
 * @param contentRange 内容范围字符串
 *
 * @return 返回生成的HTTP响应头字符串
 */
std::string HandleSend::getMessageHeader(const std::string &contentLength, const std::string &contentType, const std::string &redirectLoction, const std::string &contentRange, const std::string &extraHeader, const std::string &downloadFilename){
    std::string headerOpt;

    // 添加消息体长度字段
    if(contentLength != ""){
        headerOpt += "Content-Length: " + contentLength + "\r\n";
    }

    // 添加消息体类型字段
    if(contentType != ""){
        if(contentType == "html"){
            headerOpt += "Content-Type: text/html;charset=UTF-8\r\n";     // 发送网页时指定的类型
        }else if(contentType == "file"){
            headerOpt += "Content-Type: application/octet-stream\r\n";    // 发送文件时指定的类型
        }
    }

    // 添加重定向位置字段
    if(redirectLoction != ""){
        headerOpt += "Location: " + redirectLoction + "\r\n";
    }

    // 添加文件范围的字段
    if(contentRange != ""){
        headerOpt += "Content-Range: " + contentRange + "\r\n";
    }

    if(downloadFilename != ""){
        headerOpt += "Content-Disposition: attachment; filename=\"" + urlEncode(downloadFilename) + "\"\r\n";
    }

    // 添加额外响应头，用于登录接口设置 Cookie 等场景
    if(extraHeader != ""){
        headerOpt += extraHeader;
    }

    headerOpt += "Connection: keep-alive\r\n";

    return headerOpt;
}

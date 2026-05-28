#include "log.h"

#include <cstdio>
#include <fstream>
#include <mutex>
#include <sys/stat.h>
#include <sys/time.h>

namespace{

enum LogLevel{
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_ERROR = 2
};

LogLevel g_minLogLevel = LOG_INFO;

LogLevel parseLogLevel(const std::string &logLevel){
    if(logLevel == "DEBUG" || logLevel == "debug"){
        return LOG_DEBUG;
    }
    if(logLevel == "ERROR" || logLevel == "error"){
        return LOG_ERROR;
    }
    return LOG_INFO;
}

bool shouldWriteLine(const std::string &line){
    LogLevel lineLevel = LOG_INFO;
    if(line.find("[debug]:") != std::string::npos){
        lineLevel = LOG_DEBUG;
    }else if(line.find("[erro]:") != std::string::npos || line.find("[error]:") != std::string::npos){
        lineLevel = LOG_ERROR;
    }
    return lineLevel >= g_minLogLevel;
}

// 用于将一份日志输出同时写入控制台和日志文件
class TeeStreamBuf : public std::streambuf{
public:
    TeeStreamBuf(std::streambuf *consoleBuf, std::streambuf *fileBuf)
        : m_consoleBuf(consoleBuf), m_fileBuf(fileBuf){
    }

protected:
    virtual int overflow(int ch) override{
        if(ch == EOF){
            return !EOF;
        }

        std::lock_guard<std::mutex> locker(m_mutex);
        m_line.push_back(static_cast<char>(ch));
        if(ch == '\n'){
            if(!writeLine()){
                return EOF;
            }
        }
        return ch;
    }

    virtual std::streamsize xsputn(const char *s, std::streamsize count) override{
        std::lock_guard<std::mutex> locker(m_mutex);
        for(std::streamsize i = 0; i < count; ++i){
            m_line.push_back(s[i]);
            if(s[i] == '\n' && !writeLine()){
                return 0;
            }
        }
        return count;
    }

    virtual int sync() override{
        std::lock_guard<std::mutex> locker(m_mutex);
        if(!m_line.empty() && !writeLine()){
            return -1;
        }
        int consoleRet = m_consoleBuf->pubsync();
        int fileRet = m_fileBuf->pubsync();
        if(consoleRet != 0 || fileRet != 0){
            return -1;
        }
        return 0;
    }

private:
    bool writeLine(){
        if(!shouldWriteLine(m_line)){
            m_line.clear();
            return true;
        }
        std::streamsize count = static_cast<std::streamsize>(m_line.size());
        std::streamsize consoleRet = m_consoleBuf->sputn(m_line.c_str(), count);
        std::streamsize fileRet = m_fileBuf->sputn(m_line.c_str(), count);
        m_line.clear();
        return consoleRet == count && fileRet == count;
    }

    std::streambuf *m_consoleBuf;
    std::streambuf *m_fileBuf;
    std::mutex m_mutex;
    std::string m_line;
};

std::ofstream g_logFile;
std::streambuf *g_consoleBuf = nullptr;
TeeStreamBuf *g_teeBuf = nullptr;

// 如果日志路径包含目录，先创建一级日志目录
void createLogDir(const std::string &logFilePath){
    std::string::size_type slashIndex = logFilePath.find_last_of('/');
    if(slashIndex == std::string::npos){
        return;
    }

    std::string logDir = logFilePath.substr(0, slashIndex);
    if(logDir.empty()){
        return;
    }
    mkdir(logDir.c_str(), 0755);
}

void rotateLogFile(const std::string &logFilePath){
    struct stat logStat;
    const off_t maxLogSize = 10 * 1024 * 1024;
    if(stat(logFilePath.c_str(), &logStat) != 0 || logStat.st_size < maxLogSize){
        return;
    }

    std::string rotatedPath = logFilePath + ".1";
    std::remove(rotatedPath.c_str());
    std::rename(logFilePath.c_str(), rotatedPath.c_str());
}

}

void setLogLevel(const std::string &logLevel){
    g_minLogLevel = parseLogLevel(logLevel);
}

// 以 "09:50:19.0619 2022-09-26 [logType]: " 格式返回当前的时间和输出类型，logType 指定输出的类型：
// init  : 表示服务器的初始化过程
// error : 表示服务器运行中的出错消息
// info  : 表示程序的运行信息
std::string outHead(const std::string &logType){
    // 获取并输出时间
    auto now = std::chrono::system_clock::now();
    time_t tt = std::chrono::system_clock::to_time_t(now);
    struct tm time_tm;
    localtime_r(&tt, &time_tm);
    
    struct timeval time_usec;
    gettimeofday(&time_usec, NULL);

    char strTime[30] = { 0 };
    //16:31:15.695681 2025-05-10
    snprintf(strTime, sizeof(strTime), "%02d:%02d:%02d.%05ld %d-%02d-%02d",
            time_tm.tm_hour, time_tm.tm_min, time_tm.tm_sec, static_cast<long>(time_usec.tv_usec),
            time_tm.tm_year + 1900, time_tm.tm_mon + 1, time_tm.tm_mday);

    std::string outStr;
    // 添加时间部分
    outStr += strTime;
    // 根据传入的参数指定输出的类型
    if(logType == "init"){
        outStr += " [init]: ";
    }else if(logType == "error"){
        outStr += " [error]: ";
    }else{
        outStr += " [info]: ";
    }
    //得到16:31:15.695681 2025-05-10 [info]:
    return outStr;
}

// 初始化日志文件，将 std::cout 输出同时写入控制台和日志文件
int initLogFile(const std::string &logFilePath){
    if(logFilePath.empty()){
        return 0;
    }

    createLogDir(logFilePath);
    rotateLogFile(logFilePath);
    if(g_logFile.is_open()){
        g_logFile.close();
    }
    g_logFile.open(logFilePath.c_str(), std::ios::out | std::ios::app);
    if(!g_logFile){
        std::cout << outHead("error") << "日志文件打开失败：" << logFilePath << std::endl;
        return -1;
    }

    if(g_consoleBuf == nullptr){
        g_consoleBuf = std::cout.rdbuf();
    }

    if(g_teeBuf != nullptr && g_consoleBuf != nullptr){
        std::cout.rdbuf(g_consoleBuf);
    }
    delete g_teeBuf;
    g_teeBuf = new TeeStreamBuf(g_consoleBuf, g_logFile.rdbuf());
    std::cout.rdbuf(g_teeBuf);
    std::cout << outHead("info") << "日志文件初始化成功：" << logFilePath << std::endl;
    return 0;
}


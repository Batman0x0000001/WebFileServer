#include "config.h"

#include <cstdlib>
#include <fstream>
#include <iostream>

#include "../utils/log.h"
#include "../utils/string_utils.h"

namespace{

// 只接受正整数；配置非法时继续使用已有默认值。
int parsePositiveInt(const std::string &value, int defaultValue){
    char *end = nullptr;
    long parsed = std::strtol(value.c_str(), &end, 10);
    if(end == value.c_str() || *end != '\0' || parsed <= 0){
        return defaultValue;
    }
    return static_cast<int>(parsed);
}

long long parsePositiveLongLong(const std::string &value, long long defaultValue){
    char *end = nullptr;
    long long parsed = std::strtoll(value.c_str(), &end, 10);
    if(end == value.c_str() || *end != '\0' || parsed <= 0){
        return defaultValue;
    }
    return parsed;
}

// 支持常见布尔写法，无法识别时保留已有默认值。
bool parseBool(const std::string &value, bool defaultValue){
    if(value == "true" || value == "1" || value == "yes"){
        return true;
    }
    if(value == "false" || value == "0" || value == "no"){
        return false;
    }
    return defaultValue;
}

}

AppConfig::AppConfig()
    : m_port(8888), m_threadNum(4), m_idleTimeout(60), m_timerInterval(5),
      m_maxConnection(1024), m_storageDir("filedir"), m_logFile("logs/server.log"),
      m_mysqlEnable(false), m_mysqlHost("127.0.0.1"),
      m_mysqlPort(3306), m_mysqlUser("root"), m_mysqlPassword(""), m_mysqlDatabase("web_file_server"),
      m_redisEnable(false), m_redisHost("127.0.0.1"), m_redisPort(6379), m_redisPassword(""),
      m_sessionTtl(3600), m_fileListCacheTtl(60), m_maxUploadSize(1073741824LL), m_logLevel("INFO"){
}

AppConfig& AppConfig::instance(){
    static AppConfig config;
    return config;
}

bool AppConfig::load(const std::string &configPath){
    std::ifstream configStream(configPath);
    if(!configStream){
        std::cout << outHead("info") << "配置文件 " << configPath << " 不存在，使用默认配置" << std::endl;
        return false;
    }

    //一行一行读取，trim() 去除每行首尾的空白字符。
    std::string line;
    while(std::getline(configStream, line)){
        std::string cleanLine = trim(line);
        
        //跳过两类行：空行 或者# 开头的注释行
        if(cleanLine.empty() || cleanLine[0] == '#'){
            continue;
        }

        //找到 = 的位置，找不到则跳过该行。
        std::string::size_type equalIndex = cleanLine.find('=');
        if(equalIndex == std::string::npos){
            continue;
        }

        std::string key = trim(cleanLine.substr(0, equalIndex));
        std::string value = trim(cleanLine.substr(equalIndex + 1));
        setValue(key, value);
    }

    std::cout << outHead("info") << "配置加载完成：port=" << m_port
              << ", thread_num=" << m_threadNum
              << ", idle_timeout=" << m_idleTimeout
              << ", timer_interval=" << m_timerInterval
              << ", max_connection=" << m_maxConnection
              << ", storage_dir=" << m_storageDir
              << ", log_file=" << m_logFile
              << ", mysql_enable=" << (m_mysqlEnable ? "true" : "false")
              << ", redis_enable=" << (m_redisEnable ? "true" : "false")
              << ", max_upload_size=" << m_maxUploadSize
              << ", log_level=" << m_logLevel << std::endl;
    return true;
}

int AppConfig::port() const{
    return m_port;
}

int AppConfig::threadNum() const{
    return m_threadNum;
}

int AppConfig::idleTimeout() const{
    return m_idleTimeout;
}

int AppConfig::timerInterval() const{
    return m_timerInterval;
}

int AppConfig::maxConnection() const{
    return m_maxConnection;
}

const std::string& AppConfig::storageDir() const{
    return m_storageDir;
}

const std::string& AppConfig::logFile() const{
    return m_logFile;
}

bool AppConfig::mysqlEnable() const{
    return m_mysqlEnable;
}

const std::string& AppConfig::mysqlHost() const{
    return m_mysqlHost;
}

int AppConfig::mysqlPort() const{
    return m_mysqlPort;
}

const std::string& AppConfig::mysqlUser() const{
    return m_mysqlUser;
}

const std::string& AppConfig::mysqlPassword() const{
    return m_mysqlPassword;
}

const std::string& AppConfig::mysqlDatabase() const{
    return m_mysqlDatabase;
}

bool AppConfig::redisEnable() const{
    return m_redisEnable;
}

const std::string& AppConfig::redisHost() const{
    return m_redisHost;
}

int AppConfig::redisPort() const{
    return m_redisPort;
}

const std::string& AppConfig::redisPassword() const{
    return m_redisPassword;
}

int AppConfig::sessionTtl() const{
    return m_sessionTtl;
}

int AppConfig::fileListCacheTtl() const{
    return m_fileListCacheTtl;
}

long long AppConfig::maxUploadSize() const{
    return m_maxUploadSize;
}

const std::string& AppConfig::logLevel() const{
    return m_logLevel;
}

void AppConfig::setValue(const std::string &key, const std::string &value){
    if(key == "port"){
        m_port = parsePositiveInt(value, m_port);
    }else if(key == "thread_num"){
        m_threadNum = parsePositiveInt(value, m_threadNum);
    }else if(key == "idle_timeout"){
        m_idleTimeout = parsePositiveInt(value, m_idleTimeout);
    }else if(key == "timer_interval"){
        m_timerInterval = parsePositiveInt(value, m_timerInterval);
    }else if(key == "max_connection"){
        m_maxConnection = parsePositiveInt(value, m_maxConnection);
    }else if(key == "storage_dir" && !value.empty()){
        m_storageDir = value;
    }else if(key == "log_file" && !value.empty()){
        m_logFile = value;
    }else if(key == "mysql_enable"){
        m_mysqlEnable = parseBool(value, m_mysqlEnable);
    }else if(key == "mysql_host" && !value.empty()){
        m_mysqlHost = value;
    }else if(key == "mysql_port"){
        m_mysqlPort = parsePositiveInt(value, m_mysqlPort);
    }else if(key == "mysql_user" && !value.empty()){
        m_mysqlUser = value;
    }else if(key == "mysql_password"){
        m_mysqlPassword = value;
    }else if(key == "mysql_database" && !value.empty()){
        m_mysqlDatabase = value;
    }else if(key == "redis_enable"){
        m_redisEnable = parseBool(value, m_redisEnable);
    }else if(key == "redis_host" && !value.empty()){
        m_redisHost = value;
    }else if(key == "redis_port"){
        m_redisPort = parsePositiveInt(value, m_redisPort);
    }else if(key == "redis_password"){
        m_redisPassword = value;
    }else if(key == "session_ttl"){
        m_sessionTtl = parsePositiveInt(value, m_sessionTtl);
    }else if(key == "file_list_cache_ttl"){
        m_fileListCacheTtl = parsePositiveInt(value, m_fileListCacheTtl);
    }else if(key == "max_upload_size"){
        m_maxUploadSize = parsePositiveLongLong(value, m_maxUploadSize);
    }else if(key == "log_level" && !value.empty()){
        m_logLevel = value;
    }
}

#include "mysqlclient.h"

#include <mysql.h>
#include <iostream>
#include <vector>

#include "../config/config.h"
#include "../utils/log.h"

MysqlClient::MysqlClient() : m_isReady(false){
}

MysqlClient::~MysqlClient(){
    for(auto mysql : m_pool){
        closeConnection(mysql);
    }
    m_pool.clear();
}

MysqlClient& MysqlClient::instance(){
    static MysqlClient client;
    return client;
}

MysqlClient::ConnectionGuard::ConnectionGuard(MysqlClient &client)
    : m_client(client), m_mysql(client.acquireConnection()){
}

MysqlClient::ConnectionGuard::~ConnectionGuard(){
    m_client.releaseConnection(m_mysql);
}

void* MysqlClient::ConnectionGuard::get() const{
    return m_mysql;
}

MysqlClient::ConnectionGuard::operator bool() const{
    return m_mysql != nullptr;
}

bool MysqlClient::init(){
    std::lock_guard<std::mutex> locker(m_mutex);
    if(!AppConfig::instance().mysqlEnable()){
        std::cout << outHead("error") << "MySQL 未启用" << std::endl;
        return false;
    }

    int poolSize = AppConfig::instance().threadNum();
    for(int i = 0; i < poolSize; ++i){
        void *conn = createConnection();
        if(conn == nullptr){
            for(auto mysql : m_pool){
                closeConnection(mysql);
            }
            m_pool.clear();
            m_isReady = false;
            return false;
        }
        m_pool.push_back(conn);
    }

    m_isReady = true;
    std::cout << outHead("info") << "MySQL 连接池初始化成功，连接数：" << poolSize << std::endl;

    return true;
}

void* MysqlClient::createConnection(){
    MYSQL *mysql = mysql_init(nullptr);
    if(mysql == nullptr){
        std::cout << outHead("error") << "MySQL 初始化失败" << std::endl;
        return nullptr;
    }

    mysql_options(mysql, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    MYSQL *conn = mysql_real_connect(mysql,
                                     AppConfig::instance().mysqlHost().c_str(),
                                     AppConfig::instance().mysqlUser().c_str(),
                                     AppConfig::instance().mysqlPassword().c_str(),
                                     AppConfig::instance().mysqlDatabase().c_str(),
                                     AppConfig::instance().mysqlPort(),
                                     nullptr, 0);
    if(conn == nullptr){
        std::cout << outHead("error") << "MySQL 连接失败：" << mysql_error(mysql) << std::endl;
        mysql_close(mysql);
        return nullptr;
    }

    return mysql;
}

void MysqlClient::closeConnection(void *mysql){
    if(mysql != nullptr){
        mysql_close(static_cast<MYSQL*>(mysql));
    }
}

void* MysqlClient::acquireConnection(){
    std::unique_lock<std::mutex> locker(m_mutex);
    m_cond.wait(locker, [this](){
        return !m_isReady || !m_pool.empty();
    });
    if(!m_isReady){
        return nullptr;
    }
    void *mysql = m_pool.back();
    m_pool.pop_back();
    if(mysql_ping(static_cast<MYSQL*>(mysql)) != 0){
        std::cout << outHead("error") << "MySQL 连接已断开，尝试重连" << std::endl;
        closeConnection(mysql);
        mysql = createConnection();
    }
    return mysql;
}

void MysqlClient::releaseConnection(void *mysql){
    if(mysql == nullptr){
        return;
    }
    {
        std::lock_guard<std::mutex> locker(m_mutex);
        m_pool.push_back(mysql);
    }
    m_cond.notify_one();
}

std::string MysqlClient::escape(void *mysql, const std::string &value){
    if(mysql == nullptr){
        return value;
    }

    std::vector<char> escaped(value.size() * 2 + 1);
    unsigned long len = mysql_real_escape_string(static_cast<MYSQL*>(mysql), escaped.data(), value.c_str(), value.size());
    return std::string(escaped.data(), len);
}

bool MysqlClient::execute(void *mysql, const std::string &sql){
    if(mysql == nullptr){
        return false;
    }
    int ret = mysql_query(static_cast<MYSQL*>(mysql), sql.c_str());
    if(ret != 0){
        std::cout << outHead("error") << "MySQL 执行失败：" << mysql_error(static_cast<MYSQL*>(mysql)) << std::endl;
        return false;
    }
    return true;
}

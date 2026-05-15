#include "mysqlclient.h"

#include <mysql.h>
#include <iostream>
#include <vector>

#include "../config/config.h"
#include "../utils/utils.h"

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

bool MysqlClient::userExists(const std::string &username){
    void *mysql = acquireConnection();
    if(mysql == nullptr){
        return false;
    }

    std::string sql = "SELECT id FROM users WHERE username='" + escape(mysql, username) + "' LIMIT 1";
    if(mysql_query(static_cast<MYSQL*>(mysql), sql.c_str()) != 0){
        std::cout << outHead("error") << "MySQL 查询用户失败：" << mysql_error(static_cast<MYSQL*>(mysql)) << std::endl;
        releaseConnection(mysql);
        return false;
    }

    MYSQL_RES *res = mysql_store_result(static_cast<MYSQL*>(mysql));
    bool exists = (res != nullptr && mysql_num_rows(res) > 0);
    if(res != nullptr){
        mysql_free_result(res);
    }
    releaseConnection(mysql);
    return exists;
}

bool MysqlClient::saveUser(const std::string &username, const std::string &passwordHash){
    void *mysql = acquireConnection();
    if(mysql == nullptr){
        return false;
    }
    std::string sql = "INSERT INTO users(username,password_hash) VALUES('" +
                      escape(mysql, username) + "','" + escape(mysql, passwordHash) + "')";
    bool ok = execute(mysql, sql);
    releaseConnection(mysql);
    return ok;
}

bool MysqlClient::getUserPasswordHash(const std::string &username, std::string &passwordHash){
    void *mysql = acquireConnection();
    if(mysql == nullptr){
        return false;
    }

    std::string sql = "SELECT password_hash FROM users WHERE username='" + escape(mysql, username) + "' LIMIT 1";
    if(mysql_query(static_cast<MYSQL*>(mysql), sql.c_str()) != 0){
        std::cout << outHead("error") << "MySQL 查询用户密码失败：" << mysql_error(static_cast<MYSQL*>(mysql)) << std::endl;
        releaseConnection(mysql);
        return false;
    }

    MYSQL_RES *res = mysql_store_result(static_cast<MYSQL*>(mysql));
    bool found = false;
    if(res != nullptr){
        MYSQL_ROW row = mysql_fetch_row(res);
        if(row != nullptr && row[0] != nullptr){
            passwordHash = row[0];
            found = true;
        }
        mysql_free_result(res);
    }
    releaseConnection(mysql);
    return found;
}

bool MysqlClient::upsertFileMeta(const std::string &username, const std::string &filename,
                                 unsigned long fileSize, size_t fileHash, long uploadTime){
    void *mysql = acquireConnection();
    if(mysql == nullptr){
        return false;
    }
    std::string sql = "INSERT INTO files(username,filename,file_size,file_hash,upload_time,download_count) VALUES('" +
                      escape(mysql, username) + "','" + escape(mysql, filename) + "'," + std::to_string(fileSize) + ",'" +
                      std::to_string(fileHash) + "',FROM_UNIXTIME(" + std::to_string(uploadTime) + "),0) "
                      "ON DUPLICATE KEY UPDATE file_size=VALUES(file_size),file_hash=VALUES(file_hash),"
                      "upload_time=VALUES(upload_time)";
    bool ok = execute(mysql, sql);
    releaseConnection(mysql);
    return ok;
}

bool MysqlClient::increaseDownloadCount(const std::string &username, const std::string &filename){
    void *mysql = acquireConnection();
    if(mysql == nullptr){
        return false;
    }
    std::string sql = "UPDATE files SET download_count=download_count+1 WHERE username='" +
                      escape(mysql, username) + "' AND filename='" + escape(mysql, filename) + "'";
    bool ok = execute(mysql, sql);
    releaseConnection(mysql);
    return ok;
}

bool MysqlClient::removeFileMeta(const std::string &username, const std::string &filename){
    void *mysql = acquireConnection();
    if(mysql == nullptr){
        return false;
    }
    std::string sql = "DELETE FROM files WHERE username='" + escape(mysql, username) +
                      "' AND filename='" + escape(mysql, filename) + "'";
    bool ok = execute(mysql, sql);
    releaseConnection(mysql);
    return ok;
}

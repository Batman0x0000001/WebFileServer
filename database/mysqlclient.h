#ifndef MYSQLCLIENT_H
#define MYSQLCLIENT_H

#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

// MySQL 连接池封装。
// MySQL 是必需依赖；初始化失败时 main 直接退出。
class MysqlClient{
public:
    static MysqlClient& instance();

    bool init();

    bool userExists(const std::string &username);
    bool saveUser(const std::string &username, const std::string &passwordHash);
    bool getUserPasswordHash(const std::string &username, std::string &passwordHash);

    bool upsertFileMeta(const std::string &username, const std::string &filename,
                        unsigned long fileSize, size_t fileHash, long uploadTime);
    bool increaseDownloadCount(const std::string &username, const std::string &filename);
    bool removeFileMeta(const std::string &username, const std::string &filename);

private:
    MysqlClient();
    ~MysqlClient();

    MysqlClient(const MysqlClient&) = delete;
    MysqlClient& operator=(const MysqlClient&) = delete;

    void* createConnection();
    void closeConnection(void *mysql);
    void* acquireConnection();                 // 从连接池取出一个连接；连接池关闭时返回 nullptr
    void releaseConnection(void *mysql);       // 将连接归还连接池，并唤醒等待线程
    std::string escape(void *mysql, const std::string &value);
    bool execute(void *mysql, const std::string &sql);

    bool m_isReady;
    std::vector<void*> m_pool;
    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
};

#endif

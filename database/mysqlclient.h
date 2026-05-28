/*  文件说明：
 *  1. 定义 MySQL 全局连接池和用户、文件元信息数据库操作接口。
 *  2. main.cpp 启动时初始化连接池；认证模块和文件服务模块通过单例访问数据库。
 *  3. 用户相关接口负责用户名存在性、密码哈希保存和密码哈希查询。
 *  4. 文件相关接口负责 upsert 元信息、查询列表、增加下载次数和软删除记录。
 */
#ifndef MYSQLCLIENT_H
#define MYSQLCLIENT_H

#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

struct FileMeta{
    // 文件列表接口返回给 Qt 客户端的数据库元信息。
    std::string filename;
    unsigned long fileSize;
    std::string uploadTime;
    unsigned long downloadCount;
};

// MySQL 连接池封装。
// MySQL 是必需依赖；初始化失败时 main 直接退出。
class MysqlClient{
public:
    // 返回全局单例连接池。
    static MysqlClient& instance();

    // 按配置初始化连接池并创建必要表结构。
    bool init();

    // 用户认证相关的数据库操作。
    bool userExists(const std::string &username);
    bool saveUser(const std::string &username, const std::string &passwordHash);
    bool getUserPasswordHash(const std::string &username, std::string &passwordHash);

    // 文件元信息操作；删除只做软删除，不删除磁盘文件。
    bool upsertFileMeta(const std::string &username, const std::string &filename,
                        unsigned long fileSize, size_t fileHash, long uploadTime);
    bool getFileMetas(const std::string &username, std::vector<FileMeta> &metas);
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

    class ConnectionGuard{
    public:
        explicit ConnectionGuard(MysqlClient &client);
        ~ConnectionGuard();

        ConnectionGuard(const ConnectionGuard&) = delete;
        ConnectionGuard& operator=(const ConnectionGuard&) = delete;

        void* get() const;
        explicit operator bool() const;

    private:
        MysqlClient &m_client;
        void *m_mysql;
    };

    std::string escape(void *mysql, const std::string &value);
    bool execute(void *mysql, const std::string &sql);

    bool m_isReady;
    std::vector<void*> m_pool;
    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
};

#endif

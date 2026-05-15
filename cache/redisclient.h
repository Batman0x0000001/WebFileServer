#ifndef REDISCLIENT_H
#define REDISCLIENT_H

#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

// Redis 连接池封装。
// Redis 是必需依赖；当前用于登录 session 和文件列表缓存。
class RedisClient{
public:
    static RedisClient& instance();

    bool init();

    bool setex(const std::string &key, int ttl, const std::string &value);
    bool get(const std::string &key, std::string &value);
    bool del(const std::string &key);

private:
    RedisClient();
    ~RedisClient();

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    void* createConnection();
    void closeConnection(void *context);
    void* acquireConnection();                 // 从连接池取出一个连接；连接池关闭时返回 nullptr
    void releaseConnection(void *context);     // 将连接归还连接池，并唤醒等待线程

    bool m_isReady;
    std::vector<void*> m_pool;
    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
};

#endif

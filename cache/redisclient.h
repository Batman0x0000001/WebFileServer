/*  文件说明：
 *  1. 定义 Redis 全局连接池和基础 key-value 操作接口。
 *  2. auth 模块用 Redis 保存 session:<token> -> username 登录态。
 *  3. fileservice 模块用 Redis 删除文件列表缓存键，保证上传和删除后的列表失效。
 *  4. ConnectionGuard 通过 RAII 自动归还连接，避免提前返回导致连接泄漏。
 */
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
    // 返回全局 Redis 连接池。
    static RedisClient& instance();

    // 按配置初始化连接池。
    bool init();

    // 写入带过期时间的键，当前用于 session 和文件列表缓存。
    bool setex(const std::string &key, int ttl, const std::string &value);
    // 读取缓存值；键不存在或读取失败时返回 false。
    bool get(const std::string &key, std::string &value);
    // 删除缓存键，用于退出登录或文件列表失效。
    bool del(const std::string &key);

private:
    class ConnectionGuard{
    public:
        explicit ConnectionGuard(RedisClient &client);
        ~ConnectionGuard();

        void* get() const;
        explicit operator bool() const;

    private:
        RedisClient &m_client;
        void *m_context;
    };

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

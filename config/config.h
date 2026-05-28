/*  文件说明：
 *  1. 定义服务端全局配置单例 AppConfig。
 *  2. main.cpp 启动时从 config/server.conf 加载配置，后续模块通过 getter 读取运行参数。
 *  3. 配置覆盖监听端口、线程池、连接超时、存储目录、MySQL、Redis、日志和上传大小限制。
 *  4. 配置文件中缺失或非法的字段保留默认值，未知字段会被忽略。
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <string>

// 全局运行配置。
// 启动时从 config/server.conf 加载，缺失或非法字段保留构造函数中的默认值。
class AppConfig{
public:
    // 返回全局配置单例。
    static AppConfig& instance();

    // 从 key=value 配置文件加载配置；无法识别的键会被忽略。
    bool load(const std::string &configPath);

    // 以下 getter 暴露运行参数，配置非法时返回构造函数中的默认值。
    int port() const;
    int threadNum() const;
    int idleTimeout() const;
    int timerInterval() const;
    int maxConnection() const;
    const std::string& storageDir() const;
    const std::string& logFile() const;
    bool mysqlEnable() const;
    const std::string& mysqlHost() const;
    int mysqlPort() const;
    const std::string& mysqlUser() const;
    const std::string& mysqlPassword() const;
    const std::string& mysqlDatabase() const;
    bool redisEnable() const;
    const std::string& redisHost() const;
    int redisPort() const;
    const std::string& redisPassword() const;
    int sessionTtl() const;
    int fileListCacheTtl() const;
    long long maxUploadSize() const;
    const std::string& logLevel() const;

private:
    AppConfig();

    AppConfig(const AppConfig&) = delete;
    AppConfig& operator=(const AppConfig&) = delete;

    void setValue(const std::string &key, const std::string &value);

    int m_port;
    int m_threadNum;
    int m_idleTimeout;
    int m_timerInterval;
    int m_maxConnection;
    std::string m_storageDir;
    std::string m_logFile;
    bool m_mysqlEnable;
    std::string m_mysqlHost;
    int m_mysqlPort;
    std::string m_mysqlUser;
    std::string m_mysqlPassword;
    std::string m_mysqlDatabase;
    bool m_redisEnable;
    std::string m_redisHost;
    int m_redisPort;
    std::string m_redisPassword;
    int m_sessionTtl;
    int m_fileListCacheTtl;
    long long m_maxUploadSize;
    std::string m_logLevel;
};

#endif

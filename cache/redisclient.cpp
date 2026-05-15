#include "redisclient.h"

#include <hiredis/hiredis.h>

#include <iostream>

#include "../config/config.h"
#include "../utils/utils.h"

RedisClient::RedisClient() : m_isReady(false){
}

RedisClient::~RedisClient(){
    for(auto context : m_pool){
        closeConnection(context);
    }
    m_pool.clear();
}

RedisClient& RedisClient::instance(){
    static RedisClient client;
    return client;
}

bool RedisClient::init(){
    std::lock_guard<std::mutex> locker(m_mutex);
    if(!AppConfig::instance().redisEnable()){
        std::cout << outHead("error") << "Redis 未启用" << std::endl;
        return false;
    }

    int poolSize = AppConfig::instance().threadNum();
    for(int i = 0; i < poolSize; ++i){
        void *context = createConnection();
        if(context == nullptr){
            for(auto conn : m_pool){
                closeConnection(conn);
            }
            m_pool.clear();
            m_isReady = false;
            return false;
        }
        m_pool.push_back(context);
    }

    m_isReady = true;
    std::cout << outHead("info") << "Redis 连接池初始化成功，连接数：" << poolSize << std::endl;
    return true;
}

void* RedisClient::createConnection(){
    redisContext *context = redisConnect(AppConfig::instance().redisHost().c_str(), AppConfig::instance().redisPort());
    if(context == nullptr || context->err){
        if(context != nullptr){
            std::cout << outHead("error") << "Redis 连接失败：" << context->errstr << std::endl;
            redisFree(context);
        }else{
            std::cout << outHead("error") << "Redis 连接失败：无法创建连接上下文" << std::endl;
        }
        return nullptr;
    }

    if(!AppConfig::instance().redisPassword().empty()){
        redisReply *reply = static_cast<redisReply*>(redisCommand(context, "AUTH %s", AppConfig::instance().redisPassword().c_str()));
        if(reply == nullptr || reply->type == REDIS_REPLY_ERROR){
            std::cout << outHead("error") << "Redis AUTH 失败" << std::endl;
            if(reply != nullptr){
                freeReplyObject(reply);
            }
            redisFree(context);
            return nullptr;
        }
        freeReplyObject(reply);
    }

    return context;
}

void RedisClient::closeConnection(void *context){
    if(context != nullptr){
        redisFree(static_cast<redisContext*>(context));
    }
}

void* RedisClient::acquireConnection(){
    std::unique_lock<std::mutex> locker(m_mutex);
    m_cond.wait(locker, [this](){
        return !m_isReady || !m_pool.empty();
    });
    if(!m_isReady){
        return nullptr;
    }
    void *context = m_pool.back();
    m_pool.pop_back();
    return context;
}

void RedisClient::releaseConnection(void *context){
    if(context == nullptr){
        return;
    }
    {
        std::lock_guard<std::mutex> locker(m_mutex);
        m_pool.push_back(context);
    }
    m_cond.notify_one();
}

bool RedisClient::setex(const std::string &key, int ttl, const std::string &value){
    void *context = acquireConnection();
    if(context == nullptr){
        return false;
    }

    redisReply *reply = static_cast<redisReply*>(redisCommand(static_cast<redisContext*>(context),
                                                              "SETEX %s %d %s",
                                                              key.c_str(), ttl, value.c_str()));
    if(reply == nullptr){
        releaseConnection(context);
        return false;
    }
    bool ok = (reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "OK");
    freeReplyObject(reply);
    releaseConnection(context);
    return ok;
}

bool RedisClient::get(const std::string &key, std::string &value){
    void *context = acquireConnection();
    if(context == nullptr){
        return false;
    }

    redisReply *reply = static_cast<redisReply*>(redisCommand(static_cast<redisContext*>(context),
                                                              "GET %s", key.c_str()));
    if(reply == nullptr){
        releaseConnection(context);
        return false;
    }

    bool ok = false;
    if(reply->type == REDIS_REPLY_STRING){
        value.assign(reply->str, reply->len);
        ok = true;
    }
    freeReplyObject(reply);
    releaseConnection(context);
    return ok;
}

bool RedisClient::del(const std::string &key){
    void *context = acquireConnection();
    if(context == nullptr){
        return false;
    }

    redisReply *reply = static_cast<redisReply*>(redisCommand(static_cast<redisContext*>(context),
                                                              "DEL %s", key.c_str()));
    if(reply == nullptr){
        releaseConnection(context);
        return false;
    }
    freeReplyObject(reply);
    releaseConnection(context);
    return true;
}

/*  文件说明：
 *  1. 提供 MySQL 预编译语句的轻量辅助函数。
 *  2. mysqlclient_user.cpp 和 mysqlclient_file.cpp 使用这些函数绑定字符串、整数等参数。
 *  3. prepareStmt() 负责创建并编译 MYSQL_STMT，executePrepared() 负责绑定参数并执行。
 *  4. 失败时统一输出 MySQL 错误日志，调用方负责关闭语句对象。
 */
#ifndef MYSQL_STATEMENT_UTILS_H
#define MYSQL_STATEMENT_UTILS_H

#include <cstring>
#include <iostream>
#include <mysql.h>
#include <string>

#include "../utils/log.h"

inline void bindStringParam(MYSQL_BIND &bind, const std::string &value, unsigned long &length){
    std::memset(&bind, 0, sizeof(bind));
    length = value.size();
    bind.buffer_type = MYSQL_TYPE_STRING;
    bind.buffer = const_cast<char*>(value.c_str());
    bind.buffer_length = length;
    bind.length = &length;
}

// 绑定无符号长整型参数，常用于文件大小、下载次数等字段。
inline void bindUnsignedLongLongParam(MYSQL_BIND &bind, unsigned long long &value){
    std::memset(&bind, 0, sizeof(bind));
    bind.buffer_type = MYSQL_TYPE_LONGLONG;
    bind.buffer = &value;
    bind.is_unsigned = true;
}

// 绑定 long 参数，常用于 UNIX 时间戳等字段。
inline void bindLongParam(MYSQL_BIND &bind, long &value){
    std::memset(&bind, 0, sizeof(bind));
    bind.buffer_type = MYSQL_TYPE_LONG;
    bind.buffer = &value;
}

// 创建并预编译 SQL 语句，失败时返回 nullptr。
inline MYSQL_STMT* prepareStmt(MYSQL *mysql, const std::string &sql){
    MYSQL_STMT *stmt = mysql_stmt_init(mysql);
    if(stmt == nullptr){
        std::cout << outHead("error") << "MySQL 创建预编译语句失败：" << mysql_error(mysql) << std::endl;
        return nullptr;
    }
    if(mysql_stmt_prepare(stmt, sql.c_str(), sql.size()) != 0){
        std::cout << outHead("error") << "MySQL 预编译失败：" << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return nullptr;
    }
    return stmt;
}

// 绑定参数并执行预编译语句。
inline bool executePrepared(MYSQL_STMT *stmt, MYSQL_BIND *params){
    if(mysql_stmt_bind_param(stmt, params) != 0){
        std::cout << outHead("error") << "MySQL 绑定参数失败：" << mysql_stmt_error(stmt) << std::endl;
        return false;
    }
    if(mysql_stmt_execute(stmt) != 0){
        std::cout << outHead("error") << "MySQL 执行预编译语句失败：" << mysql_stmt_error(stmt) << std::endl;
        return false;
    }
    return true;
}

#endif

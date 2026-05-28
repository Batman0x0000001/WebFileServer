#include "mysqlclient.h"

#include <cstring>
#include <mysql.h>

#include "mysql_statement_utils.h"

bool MysqlClient::userExists(const std::string &username){
    ConnectionGuard guard(*this);
    if(!guard){
        return false;
    }

    MYSQL *conn = static_cast<MYSQL*>(guard.get());
    MYSQL_STMT *stmt = prepareStmt(conn, "SELECT id FROM users WHERE username=? LIMIT 1");
    if(stmt == nullptr){
        return false;
    }

    MYSQL_BIND params[1];
    unsigned long usernameLen = 0;
    bindStringParam(params[0], username, usernameLen);
    bool exists = false;
    if(executePrepared(stmt, params)){
        int userId = 0;
        MYSQL_BIND result[1];
        std::memset(result, 0, sizeof(result));
        result[0].buffer_type = MYSQL_TYPE_LONG;
        result[0].buffer = &userId;
        if(mysql_stmt_bind_result(stmt, result) == 0){
            exists = mysql_stmt_fetch(stmt) == 0;
        }
    }
    mysql_stmt_close(stmt);
    return exists;
}

bool MysqlClient::saveUser(const std::string &username, const std::string &passwordHash){
    ConnectionGuard guard(*this);
    if(!guard){
        return false;
    }
    MYSQL *conn = static_cast<MYSQL*>(guard.get());
    MYSQL_STMT *stmt = prepareStmt(conn, "INSERT INTO users(username,password_hash) VALUES(?,?)");
    if(stmt == nullptr){
        return false;
    }

    MYSQL_BIND params[2];
    unsigned long usernameLen = 0;
    unsigned long passwordHashLen = 0;
    bindStringParam(params[0], username, usernameLen);
    bindStringParam(params[1], passwordHash, passwordHashLen);
    bool ok = executePrepared(stmt, params);
    mysql_stmt_close(stmt);
    return ok;
}

bool MysqlClient::getUserPasswordHash(const std::string &username, std::string &passwordHash){
    ConnectionGuard guard(*this);
    if(!guard){
        return false;
    }

    MYSQL *conn = static_cast<MYSQL*>(guard.get());
    MYSQL_STMT *stmt = prepareStmt(conn, "SELECT password_hash FROM users WHERE username=? LIMIT 1");
    if(stmt == nullptr){
        return false;
    }

    bool found = false;
    MYSQL_BIND params[1];
    unsigned long usernameLen = 0;
    bindStringParam(params[0], username, usernameLen);
    if(executePrepared(stmt, params)){
        char hashBuf[512] = {0};
        unsigned long hashLen = 0;
        MYSQL_BIND result[1];
        std::memset(result, 0, sizeof(result));
        result[0].buffer_type = MYSQL_TYPE_STRING;
        result[0].buffer = hashBuf;
        result[0].buffer_length = sizeof(hashBuf) - 1;
        result[0].length = &hashLen;
        if(mysql_stmt_bind_result(stmt, result) == 0 && mysql_stmt_fetch(stmt) == 0){
            passwordHash.assign(hashBuf, hashLen);
            found = true;
        }
    }
    mysql_stmt_close(stmt);
    return found;
}

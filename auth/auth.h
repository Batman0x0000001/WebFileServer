/*  文件说明：
 *  1. 提供用户注册、登录校验、密码哈希、session token 创建和登录态查询能力。
 *  2. 用户名和密码哈希存储在 MySQL，登录 session 以 session:<token> 形式存储在 Redis。
 *  3. 新密码使用 PBKDF2-HMAC-SHA256；sha256Hex() 只用于兼容旧格式密码校验。
 *  4. 路由层通过 getLoginUserFromCookie() 判断接口是否具备已登录用户身份。
 */
#ifndef AUTH_H
#define AUTH_H

#include <string>

// 校验用户名是否满足注册和登录允许的字符范围。
bool isValidUsername(const std::string &username);
// 计算原始 SHA-256 十六进制字符串；保留用于兼容旧密码格式。
std::string sha256Hex(const std::string &input);
// 生成带算法、迭代次数、盐值和哈希值的 PBKDF2 密码串。
std::string hashPassword(const std::string &password);
// 校验明文密码是否匹配存储的 PBKDF2 或旧 SHA-256 哈希。
bool verifyPassword(const std::string &password, const std::string &storedHash);
// 保存新用户，内部会先哈希密码再写入数据库。
bool saveUser(const std::string &username, const std::string &password);
// 校验登录用户名和密码。
bool checkUserPassword(const std::string &username, const std::string &password);
// 创建 session token 并写入 Redis，返回给客户端作为 Cookie。
std::string createSessionToken(const std::string &username);
// 根据 Cookie 中的 token 查 Redis，成功时填充 username。
bool getUserByToken(const std::string &token, std::string &username);
// 从 Cookie 头中提取 token 并查 Redis，成功时填充 username。
bool getLoginUserFromCookie(const std::string &cookie, std::string &username);
// 删除 Redis 中的 session token，用于退出登录。
void deleteSessionToken(const std::string &token);

#endif

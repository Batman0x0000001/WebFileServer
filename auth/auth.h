#ifndef AUTH_H
#define AUTH_H

#include <string>

bool isValidUsername(const std::string &username);
bool saveUser(const std::string &username, const std::string &password);
bool checkUserPassword(const std::string &username, const std::string &password);
std::string createSessionToken(const std::string &username);
bool getUserByToken(const std::string &token, std::string &username);
void deleteSessionToken(const std::string &token);

#endif

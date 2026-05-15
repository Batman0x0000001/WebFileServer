#include "auth.h"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <random>
#include <vector>

#include "../cache/redisclient.h"
#include "../config/config.h"
#include "../database/mysqlclient.h"

namespace{

uint32_t rotr(uint32_t value, uint32_t bits){
    return (value >> bits) | (value << (32 - bits));
}

std::string bytesToHex(const unsigned char *data, size_t len){
    static const char *hex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for(size_t i = 0; i < len; ++i){
        out.push_back(hex[(data[i] >> 4) & 0x0f]);
        out.push_back(hex[data[i] & 0x0f]);
    }
    return out;
}

std::string sha256Hex(const std::string &input){
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    std::vector<unsigned char> data(input.begin(), input.end());
    uint64_t bitLen = static_cast<uint64_t>(data.size()) * 8;
    data.push_back(0x80);
    while((data.size() % 64) != 56){
        data.push_back(0);
    }
    for(int i = 7; i >= 0; --i){
        data.push_back(static_cast<unsigned char>((bitLen >> (i * 8)) & 0xff));
    }

    uint32_t h[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };

    for(size_t chunk = 0; chunk < data.size(); chunk += 64){
        uint32_t w[64] = {0};
        for(int i = 0; i < 16; ++i){
            size_t j = chunk + i * 4;
            w[i] = (static_cast<uint32_t>(data[j]) << 24) |
                   (static_cast<uint32_t>(data[j + 1]) << 16) |
                   (static_cast<uint32_t>(data[j + 2]) << 8) |
                   static_cast<uint32_t>(data[j + 3]);
        }
        for(int i = 16; i < 64; ++i){
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for(int i = 0; i < 64; ++i){
            uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = hh + s1 + ch + k[i] + w[i];
            uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = s0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    unsigned char digest[32];
    for(int i = 0; i < 8; ++i){
        digest[i * 4] = static_cast<unsigned char>((h[i] >> 24) & 0xff);
        digest[i * 4 + 1] = static_cast<unsigned char>((h[i] >> 16) & 0xff);
        digest[i * 4 + 2] = static_cast<unsigned char>((h[i] >> 8) & 0xff);
        digest[i * 4 + 3] = static_cast<unsigned char>(h[i] & 0xff);
    }
    return bytesToHex(digest, sizeof(digest));
}

std::string secureRandomHex(size_t bytes){
    std::vector<unsigned char> data(bytes);
    std::ifstream randomFile("/dev/urandom", std::ios::in | std::ios::binary);
    if(randomFile){
        randomFile.read(reinterpret_cast<char*>(data.data()), data.size());
        if(static_cast<size_t>(randomFile.gcount()) == data.size()){
            return bytesToHex(data.data(), data.size());
        }
    }

    std::random_device rd;
    for(size_t i = 0; i < data.size(); ++i){
        data[i] = static_cast<unsigned char>(rd() & 0xff);
    }
    return bytesToHex(data.data(), data.size());
}

std::string hashPassword(const std::string &password){
    std::string salt = secureRandomHex(16);
    return "sha256$" + salt + "$" + sha256Hex(salt + password);
}

bool verifyPassword(const std::string &password, const std::string &storedHash){
    std::string prefix = "sha256$";
    if(storedHash.find(prefix) != 0){
        return false;
    }
    std::string::size_type sepIndex = storedHash.find('$', prefix.size());
    if(sepIndex == std::string::npos){
        return false;
    }
    std::string salt = storedHash.substr(prefix.size(), sepIndex - prefix.size());
    std::string expected = storedHash.substr(sepIndex + 1);
    return sha256Hex(salt + password) == expected;
}

}

bool isValidUsername(const std::string &username){
    if(username.size() < 3 || username.size() > 32){
        return false;
    }

    for(const auto &ch : username){
        if(!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')){
            return false;
        }
    }
    return true;
}

bool saveUser(const std::string &username, const std::string &password){
    std::string passwordHash = hashPassword(password);

    if(MysqlClient::instance().userExists(username)){
        return false;
    }
    return MysqlClient::instance().saveUser(username, passwordHash);
}

bool checkUserPassword(const std::string &username, const std::string &password){
    std::string storedHash;
    return MysqlClient::instance().getUserPasswordHash(username, storedHash) &&
           verifyPassword(password, storedHash);
}

std::string createSessionToken(const std::string &username){
    std::string token = secureRandomHex(32);
    RedisClient::instance().setex("session:" + token, AppConfig::instance().sessionTtl(), username);
    return token;
}

bool getUserByToken(const std::string &token, std::string &username){
    if(token.empty()){
        return false;
    }
    return RedisClient::instance().get("session:" + token, username);
}

void deleteSessionToken(const std::string &token){
    if(token.empty()){
        return;
    }
    RedisClient::instance().del("session:" + token);
}

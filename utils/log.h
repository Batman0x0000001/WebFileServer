/*  文件说明：
 *  1. 提供服务端日志输出前缀、日志文件初始化和日志级别设置。
 *  2. outHead() 生成带时间和类型的日志前缀，供各模块输出到 std::cout。
 *  3. initLogFile() 将输出同时写入控制台和配置文件指定的日志文件。
 *  4. setLogLevel() 根据配置控制 DEBUG、INFO、ERROR 等日志输出级别。
 */
#ifndef LOG_H
#define LOG_H
#include <iostream>
#include <ctime>
#include <chrono>
#include <string.h>


// 返回当前时间和输出类型组成的日志前缀；logType 常用值为 init、error、info、debug。
std::string outHead(const std::string &logType);

// 初始化日志文件，将 std::cout 输出同时写入控制台和日志文件。
int initLogFile(const std::string &logFilePath);

// 设置日志级别，支持 DEBUG / INFO / ERROR。
void setLogLevel(const std::string &logLevel);

#endif

/*  文件说明：
 *  1. 定义固定大小的事件处理线程池。
 *  2. WebServer 主线程只负责 epoll_wait 和投递事件，工作线程从队列中取出 EventBase 并调用 process()。
 *  3. 事件对象使用 std::unique_ptr 管理，避免任务入队和执行过程中的所有权不清。
 *  4. 析构时会通知所有工作线程退出并 join，保证服务端关闭时事件对象安全释放。
 */
#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

class EventBase;

class ThreadPool{
public:
    // 初始化固定数量工作线程；线程启动后阻塞等待事件队列。
    ThreadPool(int threadNum);
    // 通知所有线程退出并等待 join，保证事件对象安全释放。
    ~ThreadPool();
public:
    // 向事件队列中添加一个待处理事件；返回值用于调用方判断是否入队成功。
    int appendEvent(std::unique_ptr<EventBase> event, const std::string &eventType);

private:
    // 工作线程主循环：取事件、释放队列锁、调用 EventBase::process()。
    void run(int threadNo);

private:
    int m_threadNum;                  // 线程池中的线程个数
    std::vector<std::thread> m_threads; // 保存线程池中的所有线程
    bool m_isStop;                    // 标识线程池是否正在停止
    
    std::queue<std::unique_ptr<EventBase> > m_workQueue;  // 保存所有待处理的事件
    std::mutex m_queueMutex;          // 用于互斥访问事件队列的锁
    std::condition_variable m_queueCond; // 表示队列中事件个数变化的条件变量

};


#endif

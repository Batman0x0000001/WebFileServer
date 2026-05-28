#include "threadpool.h"

#include <iostream>

#include "../event/event_handler.h"
#include "../utils/log.h"

ThreadPool::ThreadPool(int threadNum) : m_threadNum(threadNum), m_isStop(false){
    if(m_threadNum <= 0){
        throw std::runtime_error("线程数量非法");
    }

    m_threads.reserve(m_threadNum);
    for(int i = 0; i < m_threadNum; ++i){
        m_threads.emplace_back([this, i](){
            run(i + 1);
        });
    }
}

ThreadPool::~ThreadPool(){
    {
        std::lock_guard<std::mutex> locker(m_queueMutex);
        m_isStop = true;
    }
    m_queueCond.notify_all();

    for(auto &thread : m_threads){
        if(thread.joinable()){
            thread.join();
        }
    }

    while(!m_workQueue.empty()){
        m_workQueue.pop();
    }
}

int ThreadPool::appendEvent(std::unique_ptr<EventBase> event, const std::string &eventType){
    if(event == nullptr){
        return -1;
    }

    {
        std::lock_guard<std::mutex> locker(m_queueMutex);
        if(m_isStop){
            std::cout << outHead("error") << "线程池正在停止，拒绝添加新事件" << std::endl;
            return -2;
        }
        m_workQueue.push(std::move(event));
        std::cout << outHead("info") << eventType << "添加成功，线程池事件队列中剩余的事件个数：" << m_workQueue.size() << std::endl;
    }

    m_queueCond.notify_one();
    return 0;
}

void ThreadPool::run(int threadNo){
    std::cout << outHead("info") << "线程 " << threadNo << " 正在执行" << std::endl;
    while(1){
        std::unique_ptr<EventBase> curEvent;
        {
            std::unique_lock<std::mutex> locker(m_queueMutex);
            m_queueCond.wait(locker, [this](){
                return m_isStop || !m_workQueue.empty();
            });

            if(m_isStop && m_workQueue.empty()){
                std::cout << outHead("info") << "线程 " << threadNo << " 收到停止信号，退出执行" << std::endl;
                return;
            }

            curEvent = std::move(m_workQueue.front());
            m_workQueue.pop();
        }

        if(curEvent == nullptr){
            continue;
        }

        std::cout << outHead("info") << "线程 " << threadNo << " 开始处理事件" << std::endl;
        curEvent->process();
        std::cout << outHead("info") << "线程 " << threadNo << " 处理事件完成" << std::endl;
    }
}

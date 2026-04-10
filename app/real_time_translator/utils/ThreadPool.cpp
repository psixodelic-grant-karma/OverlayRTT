#include "ThreadPool.h"
#include <stdexcept>

namespace translator {

ThreadPool::ThreadPool(size_t threads) 
    : stop_(false), maxThreads_(threads > 0 ? threads : std::thread::hardware_concurrency()) 
{
    // Создаём рабочие потоки
    for (size_t i = 0; i < maxThreads_; ++i) {
        workers_.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                
                {
                    std::unique_lock lock(queueMutex_);
                    
                    // Ждём пока не будет задач или пул не остановится
                    condition_.wait(lock, [this] {
                        return stop_.load() || !tasks_.empty();
                    });
                    
                    if (stop_.load() && tasks_.empty()) {
                        return;
                    }
                    
                    // Берём задачу из очереди
                    if (!tasks_.empty()) {
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                }
                
                // Выполняем задачу вне критической секции
                if (task) {
                    task();
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    stop_.store(true);
    condition_.notify_all();
    
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::waitAll() {
    // Ждём пока очередь не опустеет
    std::unique_lock lock(queueMutex_);
    condition_.wait(lock, [this] {
        return tasks_.empty();
    });
}

size_t ThreadPool::getQueueSize() const {
    std::lock_guard lock(const_cast<std::mutex&>(queueMutex_));
    return tasks_.size();
}

size_t ThreadPool::getThreadCount() const {
    return maxThreads_;
}

void ThreadPool::stop() {
    stop_.store(true);
    condition_.notify_all();
}

} // namespace translator

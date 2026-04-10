#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>

namespace translator {

/**
 * @brief Пул потоков для параллельного выполнения задач
 */
class ThreadPool {
private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queueMutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;
    size_t maxThreads_;

public:
    /**
     * @brief Конструктор
     * @param threads количество потоков (по умолчанию - число аппаратных ядер)
     */
    explicit ThreadPool(size_t threads = 0);
    
    ~ThreadPool();
    
    // Запрет копирования
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * @brief Поставить задачу в очередь
     * @param task функция задачи
     * @return future для получения результата
     */
    template<typename F>
    std::future<std::result_of_t<F()>> enqueue(F&& task);

    /**
     * @brief Ожидать завершения всех задач
     */
    void waitAll();

    /**
     * @brief Получить размер очереди задач
     * @return количество задач в очереди
     */
    size_t getQueueSize() const;

    /**
     * @brief Получить количество потоков
     * @return количество рабочих потоков
     */
    size_t getThreadCount() const;

    /**
     * @brief Остановить пул
     */
    void stop();
};

// Реализация
template<typename F>
std::future<std::result_of_t<F()>> ThreadPool::enqueue(F&& task) {
    using ResultType = std::result_of_t<F()>;
    
    auto packagedTask = std::make_shared<std::packaged_task<ResultType()>>(
        std::forward<F>(task)
    );
    
    auto future = packagedTask->get_future();
    
    {
        std::lock_guard lock(queueMutex_);
        
        // Не принимаем новые задачи после остановки
        if (stop_.load()) {
            throw std::runtime_error("ThreadPool is stopped");
        }
        
        tasks_.emplace([packagedTask]() {
            (*packagedTask)();
        });
    }
    
    condition_.notify_one();
    return future;
}

} // namespace translator

#pragma once

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <chrono>

namespace translator {

/**
 * @brief Перезаписываемая очередь (single-slot queue)
 * 
 * Хранит только последнее значение. При push() предыдущее значение перезаписывается.
 * Это реализует механизм "throttle + pending frame" из ТЗ.
 * 
 * @tparam T тип данных
 */
template<typename T>
class LatestQueue {
private:
    std::atomic<bool> has_pending_{false};
    T pending_value_;
    std::mutex mtx_;
    std::condition_variable cv_;

public:
    LatestQueue() = default;
    ~LatestQueue() = default;
    
    // Запрет копирования
    LatestQueue(const LatestQueue&) = delete;
    LatestQueue& operator=(const LatestQueue&) = delete;
    
    // Разрешение перемещения
    LatestQueue(LatestQueue&&) = default;
    LatestQueue& operator=(LatestQueue&&) = default;

    /**
     * @brief Перезаписывает предыдущее значение (всегда хранит только последнее)
     * @param value новое значение
     */
    void push(const T& value) {
        {
            std::lock_guard lock(mtx_);
            pending_value_ = value;
            has_pending_.store(true, std::memory_order_release);
        }
        cv_.notify_one();
    }
    
    /**
     * @brief Перемещение значения
     * @param value новое значение
     */
    void push(T&& value) {
        {
            std::lock_guard lock(mtx_);
            pending_value_ = std::move(value);
            has_pending_.store(true, std::memory_order_release);
        }
        cv_.notify_one();
    }

    /**
     * @brief Неблокирующее получение
     * @param out результат
     * @return true если данные получены
     */
    bool pop(T& out) {
        if (!has_pending_.load(std::memory_order_acquire))
            return false;
        
        std::lock_guard lock(mtx_);
        if (!has_pending_.load())
            return false;
        
        out = std::move(pending_value_);
        has_pending_.store(false);
        return true;
    }

    /**
     * @brief Ожидание с таймаутом
     * @param out результат
     * @param timeout максимальное время ожидания
     * @return true если данные получены, false при таймауте
     */
    bool waitPop(T& out, std::chrono::milliseconds timeout) {
        std::unique_lock lock(mtx_);
        if (!cv_.wait_for(lock, timeout, [this] { 
            return has_pending_.load(std::memory_order_acquire); 
        })) {
            return false; // таймаут
        }
        
        out = std::move(pending_value_);
        has_pending_.store(false);
        return true;
    }

    /**
     * @brief Проверка наличия данных
     * @return true если есть непрочитанные данные
     */
    bool hasData() const {
        return has_pending_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Очистка очереди
     */
    void clear() {
        std::lock_guard lock(mtx_);
        has_pending_.store(false, std::memory_order_release);
    }
    
    /**
     * @brief Получить значение без удаления (peek)
     * @return значение если есть, пустой optional если нет
     */
    std::optional<T> peek() const {
        if (!has_pending_.load(std::memory_order_acquire))
            return std::nullopt;
        
        std::lock_guard lock(mtx_);
        if (!has_pending_.load())
            return std::nullopt;
        
        return pending_value_;
    }
};

} // namespace translator

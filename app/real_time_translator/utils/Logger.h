#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include <functional>
#include <filesystem>
#include <sstream>

namespace translator {

enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
    Critical = 4
};

/**
 * @brief Асинхронный логгер с ротацией файлов
 */
class Logger {
private:
    std::ofstream file_;
    std::mutex mutex_;
    LogLevel minLevel_ = LogLevel::Info;
    std::filesystem::path logPath_;
    size_t maxFileSize_ = 10 * 1024 * 1024; // 10 MB
    size_t maxFiles_ = 5;
    std::function<void(LogLevel, const std::string&)> callback_;

    static std::string levelToString(LogLevel level);
    static std::string getCurrentTime();

public:
    Logger();
    ~Logger();

    // Запрет копирования
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /**
     * @brief Установить минимальный уровень логирования
     * @param level минимальный уровень
     */
    void setLevel(LogLevel level);

    /**
     * @brief Установить путь к файлу логов
     * @param path путь к файлу
     */
    void setOutputFile(const std::filesystem::path& path);

    /**
     * @brief Установить максимальный размер файла логов
     * @param size размер в байтах
     */
    void setMaxFileSize(size_t size);

    /**
     * @brief Установить количество файлов при ротации
     * @param count количество файлов
     */
    void setMaxFiles(size_t count);

    /**
     * @brief Установить callback для внешней обработки
     * @param callback функция обратного вызова
     */
    void setCallback(std::function<void(LogLevel, const std::string&)> callback);

    // Методы логирования
    void debug(const std::string& module, const std::string& message);
    void info(const std::string& module, const std::string& message);
    void warning(const std::string& module, const std::string& message);
    void error(const std::string& module, const std::string& message);
    void critical(const std::string& module, const std::string& message);

    /**
     * @brief Универсальный метод логирования
     * @param level уровень
     * @param module имя модуля
     * @param message сообщение
     */
    void log(LogLevel level, const std::string& module, const std::string& message);

    /**
     * @brief Выполнить ротацию логов
     */
    void rotate();

private:
    void writeToFile(const std::string& message);
    void performRotation();
};

// Удобные макросы
#define LOG_DEBUG(module, msg) \
    do { if (auto logger = translator::Logger::instance()) logger->debug(module, msg); } while(0)

#define LOG_INFO(module, msg) \
    do { if (auto logger = translator::Logger::instance()) logger->info(module, msg); } while(0)

#define LOG_WARNING(module, msg) \
    do { if (auto logger = translator::Logger::instance()) logger->warning(module, msg); } while(0)

#define LOG_ERROR(module, msg) \
    do { if (auto logger = translator::Logger::instance()) logger->error(module, msg); } while(0)

#define LOG_CRITICAL(module, msg) \
    do { if (auto logger = translator::Logger::instance()) logger->critical(module, msg); } while(0)

} // namespace translator

#include "Logger.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <filesystem>

namespace translator {

// Статическая реализация
Logger::Logger() = default;

Logger::~Logger() {
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

void Logger::setLevel(LogLevel level) {
    minLevel_ = level;
}

void Logger::setOutputFile(const std::filesystem::path& path) {
    std::lock_guard lock(mutex_);
    
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
    
    logPath_ = path;
    
    // Создаём директорию если нужно
    if (auto parent = path.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    
    file_.open(path, std::ios::app);
    if (!file_.is_open()) {
        std::cerr << "[Logger] Failed to open log file: " << path << std::endl;
    }
}

void Logger::setMaxFileSize(size_t size) {
    maxFileSize_ = size;
}

void Logger::setMaxFiles(size_t count) {
    maxFiles_ = count;
}

void Logger::setCallback(std::function<void(LogLevel, const std::string&)> callback) {
    callback_ = callback;
}

void Logger::debug(const std::string& module, const std::string& message) {
    log(LogLevel::Debug, module, message);
}

void Logger::info(const std::string& module, const std::string& message) {
    log(LogLevel::Info, module, message);
}

void Logger::warning(const std::string& module, const std::string& message) {
    log(LogLevel::Warning, module, message);
}

void Logger::error(const std::string& module, const std::string& message) {
    log(LogLevel::Error, module, message);
}

void Logger::critical(const std::string& module, const std::string& message) {
    log(LogLevel::Critical, module, message);
}

void Logger::log(LogLevel level, const std::string& module, const std::string& message) {
    if (level < minLevel_) return;
    
    std::stringstream ss;
    ss << "[" << getCurrentTime() << "] "
       << "[" << levelToString(level) << "] "
       << "[" << module << "] "
       << message;
    
    std::string logLine = ss.str();
    
    // Вывод в консоль
    if (level >= LogLevel::Error) {
        std::cerr << logLine << std::endl;
    } else {
        std::cout << logLine << std::endl;
    }
    
    // Запись в файл
    writeToFile(logLine);
    
    // Callback
    if (callback_) {
        callback_(level, logLine);
    }
}

void Logger::writeToFile(const std::string& message) {
    std::lock_guard lock(mutex_);
    
    if (!file_.is_open()) return;
    
    // Проверка ротации
    if (file_.tellp() > static_cast<std::streampos>(maxFileSize_)) {
        performRotation();
    }
    
    file_ << message << std::endl;
    file_.flush();
}

void Logger::performRotation() {
    if (!file_.is_open()) return;
    
    file_.close();
    
    // Переименовываем старые файлы
    for (int i = static_cast<int>(maxFiles_) - 1; i > 0; --i) {
        std::filesystem::path oldFile = logPath_;
        std::filesystem::path newFile = logPath_;
        
        oldFile.replace_filename(logPath_.stem().string() + "." + std::to_string(i) + logPath_.extension().string());
        newFile.replace_filename(logPath_.stem().string() + "." + std::to_string(i + 1) + logPath_.extension().string());
        
        if (std::filesystem::exists(oldFile)) {
            std::filesystem::rename(oldFile, newFile);
        }
    }
    
    // Открываем новый файл
    file_.open(logPath_, std::ios::trunc);
}

void Logger::rotate() {
    std::lock_guard lock(mutex_);
    performRotation();
    if (!logPath_.empty()) {
        file_.open(logPath_, std::ios::trunc);
    }
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:    return "DEBUG";
        case LogLevel::Info:     return "INFO";
        case LogLevel::Warning:  return "WARN";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

std::string Logger::getCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    return ss.str();
}

} // namespace translator

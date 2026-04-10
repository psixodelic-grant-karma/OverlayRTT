#pragma once

#include "../../core/Interfaces.h"

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstdlib>

namespace translator {

/**
 * @brief Локальный переводчик Bergamot Translator
 * 
 * Вызывает bergamot бинарник через subprocess.
 * Оптимизирован для CPU - подходит для игр.
 * 
 * Требует: скачанную и патченную модель + bergamot бинарник
 */
class BergamotTranslator : public ITranslator {
private:
    // Состояние
    ModuleState state_ = ModuleState::Idle;
    std::string lastError_;
    std::atomic<bool> running_{false};
    
    // Настройки
    std::string sourceLang_ = "en";
    std::string targetLang_ = "ru";
    std::string modelConfigPath_;
    std::string bergamotPath_;
    int numThreads_ = 4;
    
    // Кэш переводов
    struct CacheEntry {
        std::string original;
        std::string translated;
        std::chrono::steady_clock::time_point timestamp;
    };
    std::vector<CacheEntry> translationCache_;
    size_t maxCacheSize_ = 1000;
    std::mutex cacheMutex_;
    
    // Статистика
    size_t totalTranslated_ = 0;
    size_t cacheHits_ = 0;

public:
    BergamotTranslator();
    ~BergamotTranslator() override;
    
    bool init() override;
    void start() override;
    void stop() override;
    
    ModuleState getState() const override { return state_; }
    std::string getLastError() const override { return lastError_; }
    
    void setSourceLanguage(const std::string& lang) override;
    void setTargetLanguage(const std::string& lang) override;
    void setInferenceParams(const InferenceParams& params) override;
    TranslatedBlock translate(const TextBlock& block) override;
    void setCacheSize(size_t size) override;
    
    // Дополнительные методы
    void setModelConfig(const std::string& path);
    void setBergamotPath(const std::string& path);
    void setNumThreads(int threads);

private:
    std::string translateText(const std::string& text);
    std::string getFromCache(const std::string& text);
    void addToCache(const std::string& original, const std::string& translated);
    std::string findModelConfig();
    std::string findBergamot();
};

// ============================================================================
// Реализация
// ============================================================================

inline BergamotTranslator::BergamotTranslator() {
    modelConfigPath_ = "./models/config.yml";
    bergamotPath_ = "./bergamot";
}

inline BergamotTranslator::~BergamotTranslator() {
    stop();
}

inline bool BergamotTranslator::init() {
    try {
        state_ = ModuleState::Idle;
        lastError_.clear();
        
        // Ищем bergamot бинарник
        bergamotPath_ = findBergamot();
        if (bergamotPath_.empty()) {
            lastError_ = "bergamot binary not found. Build bergamot-translator first.";
            std::cerr << "[BergamotTranslator] " << lastError_ << std::endl;
            return false;
        }
        
        // Ищем модель
        modelConfigPath_ = findModelConfig();
        if (modelConfigPath_.empty()) {
            lastError_ = "Model config not found. Download and patch model.";
            std::cerr << "[BergamotTranslator] " << lastError_ << std::endl;
            std::cerr << "[BergamotTranslator] See bergamot-translator/examples/run-native.sh" << std::endl;
            return false;
        }
        
        std::cout << "[BergamotTranslator] Binary: " << bergamotPath_ << std::endl;
        std::cout << "[BergamotTranslator] Model: " << modelConfigPath_ << std::endl;
        std::cout << "[BergamotTranslator] Threads: " << numThreads_ << std::endl;
        
        return true;
        
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

inline void BergamotTranslator::start() {
    if (running_.load()) return;
    
    running_.store(true);
    state_ = ModuleState::Running;
    
    std::cout << "[BergamotTranslator] Started" << std::endl;
}

inline void BergamotTranslator::stop() {
    running_.store(false);
    state_ = ModuleState::Stopped;
    
    std::cout << "[BergamotTranslator] Stopped (translated " << totalTranslated_ 
              << " blocks, cache hits: " << cacheHits_ << ")" << std::endl;
}

inline void BergamotTranslator::setSourceLanguage(const std::string& lang) {
    sourceLang_ = lang;
}

inline void BergamotTranslator::setTargetLanguage(const std::string& lang) {
    targetLang_ = lang;
}

inline void BergamotTranslator::setInferenceParams(const InferenceParams& params) {
    (void)params;
}

inline void BergamotTranslator::setCacheSize(size_t size) {
    std::lock_guard lock(cacheMutex_);
    maxCacheSize_ = size;
    
    if (translationCache_.size() > maxCacheSize_) {
        translationCache_.resize(maxCacheSize_);
    }
}

inline void BergamotTranslator::setModelConfig(const std::string& path) {
    modelConfigPath_ = path;
}

inline void BergamotTranslator::setBergamotPath(const std::string& path) {
    bergamotPath_ = path;
}

inline void BergamotTranslator::setNumThreads(int threads) {
    numThreads_ = threads > 0 ? threads : 1;
}

inline std::string BergamotTranslator::findBergamot() {
    std::vector<std::string> paths = {
        "./bergamot",
        "./models/bergamot",
        "../3rd_party/bergamot/bergamot",
        "/usr/local/bin/bergamot",
        "/tmp/bergamot"
    };
    
    for (const auto& path : paths) {
        std::ifstream f(path);
        if (f.good()) {
            return path;
        }
    }
    
    return "";
}

inline std::string BergamotTranslator::findModelConfig() {
    std::vector<std::string> paths = {
        "./models/config.yml",
        "./models/ende.student.tiny11/config.yml.bergamot.yml",
        "./ende.student.tiny11/config.yml.bergamot.yml",
        "../3rd_party/models/ende.student.tiny11/config.yml.bergamot.yml",
        "/tmp/bergamot/model.yml"
    };
    
    for (const auto& path : paths) {
        std::ifstream f(path);
        if (f.good()) {
            return path;
        }
    }
    
    return "";
}

inline std::string BergamotTranslator::getFromCache(const std::string& text) {
    std::lock_guard lock(cacheMutex_);
    
    auto now = std::chrono::steady_clock::now();
    
    for (size_t i = 0; i < translationCache_.size(); i++) {
        if (translationCache_[i].original == text) {
            translationCache_[i].timestamp = now;
            cacheHits_++;
            return translationCache_[i].translated;
        }
    }
    
    return "";
}

inline void BergamotTranslator::addToCache(const std::string& original, const std::string& translated) {
    std::lock_guard lock(cacheMutex_);
    
    if (translationCache_.size() >= maxCacheSize_) {
        auto oldest = std::min_element(translationCache_.begin(), translationCache_.end(),
            [](const CacheEntry& a, const CacheEntry& b) {
                return a.timestamp < b.timestamp;
            });
        translationCache_.erase(oldest);
    }
    
    CacheEntry entry;
    entry.original = original;
    entry.translated = translated;
    entry.timestamp = std::chrono::steady_clock::now();
    translationCache_.push_back(entry);
}

inline std::string BergamotTranslator::translateText(const std::string& text) {
    if (text.empty()) return "";
    
    // Проверяем кэш
    std::string cached = getFromCache(text);
    if (!cached.empty()) {
        return cached;
    }
    
    // Записываем текст во временный файл
    std::string inputFile = "/tmp/bergamot_input_" + std::to_string(std::time(nullptr)) + ".txt";
    std::string outputFile = "/tmp/bergamot_output_" + std::to_string(std::time(nullptr)) + ".txt";
    
    {
        std::ofstream out(inputFile);
        out << text;
    }
    
    // Вызываем bergamot
    std::string cmd = bergamotPath_ + 
                      " --model-config-paths " + modelConfigPath_ +
                      " --cpu-threads " + std::to_string(numThreads_) +
                      " < " + inputFile + 
                      " > " + outputFile + " 2>/dev/null";
    
    int result = std::system(cmd.c_str());
    
    if (result != 0) {
        std::cerr << "[BergamotTranslator] bergamot failed with code: " << result << std::endl;
        std::remove(inputFile.c_str());
        return text;
    }
    
    // Читаем результат
    std::string translated;
    {
        std::ifstream in(outputFile);
        std::getline(in, translated);
    }
    
    // Удаляем временные файлы
    std::remove(inputFile.c_str());
    std::remove(outputFile.c_str());
    
    if (!translated.empty()) {
        addToCache(text, translated);
        return translated;
    }
    
    return text;
}

inline TranslatedBlock BergamotTranslator::translate(const TextBlock& block) {
    TranslatedBlock result;
    result.id = block.id;
    result.originalText = block.text;
    result.confidence = block.confidence;
    
    if (state_ != ModuleState::Running) {
        result.translatedText = "[Translator not ready] " + block.text;
        return result;
    }
    
    result.translatedText = translateText(block.text);
    
    if (result.translatedText.empty() || result.translatedText == block.text) {
        result.translatedText = "[Translation failed] " + block.text;
    }
    
    totalTranslated_++;
    return result;
}

} // namespace translator

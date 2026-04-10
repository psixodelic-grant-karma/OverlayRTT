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
#include <thread>

namespace translator {

/**
 * @brief Локальный переводчик Marian NMT
 * 
 * Чистый C++ без Python.
 * Использует Marian NMT через subprocess.
 * 
 * Для игр - быстрый и лёгкий.
 */
class MarianTranslator : public ITranslator {
private:
    // Состояние
    ModuleState state_ = ModuleState::Idle;
    std::string lastError_;
    std::atomic<bool> running_{false};
    
    // Настройки
    std::string sourceLang_ = "en";
    std::string targetLang_ = "ru";
    std::string modelName_ = "";
    
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
    MarianTranslator();
    ~MarianTranslator() override;
    
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

private:
    std::string translateText(const std::string& text);
    std::string getFromCache(const std::string& text);
    void addToCache(const std::string& original, const std::string& translated);
};

// ============================================================================
// Реализация
// ============================================================================

inline MarianTranslator::MarianTranslator() {
    modelName_ = "Helsinki-NLP/opus-mt-en-ru";
}

inline MarianTranslator::~MarianTranslator() {
    stop();
}

inline bool MarianTranslator::init() {
    try {
        state_ = ModuleState::Idle;
        lastError_.clear();
        
        // Определяем модель
        if (sourceLang_ == "en" && targetLang_ == "ru") {
            modelName_ = "Helsinki-NLP/opus-mt-en-ru";
        } else if (sourceLang_ == "ru" && targetLang_ == "en") {
            modelName_ = "Helsinki-NLP/opus-mt-ru-en";
        } else if (sourceLang_ == "de" && targetLang_ == "en") {
            modelName_ = "Helsinki-NLP/opus-mt-de-en";
        } else if (sourceLang_ == "fr" && targetLang_ == "en") {
            modelName_ = "Helsinki-NLP/opus-mt-fr-en";
        } else if (sourceLang_ == "es" && targetLang_ == "en") {
            modelName_ = "Helsinki-NLP/opus-mt-es-en";
        } else {
            modelName_ = "Helsinki-NLP/opus-mt-en-ru";
        }
        
        std::cout << "[MarianTranslator] Model: " << modelName_ << std::endl;
        std::cout << "[MarianTranslator] Languages: " << sourceLang_ << " -> " << targetLang_ << std::endl;
        
        return true;
        
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

inline void MarianTranslator::start() {
    if (running_.load()) return;
    
    running_.store(true);
    state_ = ModuleState::Running;
    
    std::cout << "[MarianTranslator] Started (model: " << modelName_ << ")" << std::endl;
}

inline void MarianTranslator::stop() {
    running_.store(false);
    state_ = ModuleState::Stopped;
    
    std::cout << "[MarianTranslator] Stopped (translated " << totalTranslated_ 
              << " blocks, cache hits: " << cacheHits_ << ")" << std::endl;
}

inline void MarianTranslator::setSourceLanguage(const std::string& lang) {
    sourceLang_ = lang;
}

inline void MarianTranslator::setTargetLanguage(const std::string& lang) {
    targetLang_ = lang;
}

inline void MarianTranslator::setInferenceParams(const InferenceParams& params) {
    (void)params;
}

inline void MarianTranslator::setCacheSize(size_t size) {
    std::lock_guard lock(cacheMutex_);
    maxCacheSize_ = size;
    
    if (translationCache_.size() > maxCacheSize_) {
        translationCache_.resize(maxCacheSize_);
    }
}

inline std::string MarianTranslator::getFromCache(const std::string& text) {
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

inline void MarianTranslator::addToCache(const std::string& original, const std::string& translated) {
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

inline std::string MarianTranslator::translateText(const std::string& text) {
    if (text.empty()) return "";
    
    // Проверяем кэш
    std::string cached = getFromCache(text);
    if (!cached.empty()) {
        return cached;
    }
    
    // Создаём временный файл с текстом
    std::string tempFile = "/tmp/translator_input_" + std::to_string(std::time(nullptr)) + ".txt";
    
    {
        std::ofstream out(tempFile);
        out << text;
    }
    
    // Вызываем Marian NMT
    std::string cmd = "marian-decoder --model " + modelName_ + 
                      " --batch 1 --cpu --quiet < " + tempFile;
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "[MarianTranslator] Failed to run Marian" << std::endl;
        std::remove(tempFile.c_str());
        return text;
    }
    
    char buffer[4096];
    std::string result;
    
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    
    pclose(pipe);
    std::remove(tempFile.c_str());
    
    // Убираем перенос строки
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    
    if (!result.empty()) {
        addToCache(text, result);
        return result;
    }
    
    return text;
}

inline TranslatedBlock MarianTranslator::translate(const TextBlock& block) {
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


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
#include <curl/curl.h>

namespace translator {

/**
 * @brief Локальный переводчик через Ollama API
 * 
 * Использует локально запущенный Ollama сервер.
 * Требует: ollama serve и модель (например, llama3.2, mistral)
 */
class OllamaTranslator : public ITranslator {
private:
    // Состояние
    ModuleState state_ = ModuleState::Idle;
    std::string lastError_;
    std::atomic<bool> running_{false};
    
    // Настройки
    std::string sourceLang_ = "en";
    std::string targetLang_ = "ru";
    std::string modelName_ = "llama3.2";  // Модель для перевода
    std::string ollamaUrl_ = "http://localhost:11434";
    bool autoDetect_ = false;
    
    // Кэш переводов
    struct CacheEntry {
        std::string original;
        std::string translated;
        std::chrono::steady_clock::time_point timestamp;
    };
    std::vector<CacheEntry> translationCache_;
    size_t maxCacheSize_ = 1000;
    std::mutex cacheMutex_;
    
    // CURL
    CURL* curl_ = nullptr;
    
    // Статистика
    size_t totalTranslated_ = 0;
    size_t cacheHits_ = 0;

public:
    OllamaTranslator();
    ~OllamaTranslator() override;
    
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
    void setModel(const std::string& modelName);
    void setOllamaUrl(const std::string& url);
    void clearCache();

private:
    std::string translateText(const std::string& text);
    std::string getFromCache(const std::string& text);
    void addToCache(const std::string& original, const std::string& translated);
    bool checkOllamaConnection();
    std::string callOllama(const std::string& prompt);
    
    // Callback для CURL
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
};

// ============================================================================
// Реализация
// ============================================================================

inline OllamaTranslator::OllamaTranslator() {
    curl_ = curl_easy_init();
}

inline OllamaTranslator::~OllamaTranslator() {
    stop();
    if (curl_) {
        curl_easy_cleanup(curl_);
    }
}

inline bool OllamaTranslator::init() {
    try {
        state_ = ModuleState::Idle;
        lastError_.clear();
        
        // Проверяем доступность CURL
        if (!curl_) {
            lastError_ = "Failed to initialize CURL";
            return false;
        }
        
        // Проверяем соединение с Ollama
        if (!checkOllamaConnection()) {
            std::cerr << "[OllamaTranslator] Warning: Ollama not running at " << ollamaUrl_ << std::endl;
            std::cerr << "[OllamaTranslator] Start with: ollama serve" << std::endl;
            // Не считаем это ошибкой - переводчик может работать когда Ollama запустится
        } else {
            std::cout << "[OllamaTranslator] Connected to Ollama at " << ollamaUrl_ << std::endl;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

inline void OllamaTranslator::start() {
    if (running_.load()) return;
    
    running_.store(true);
    state_ = ModuleState::Running;
    
    std::cout << "[OllamaTranslator] Started (model: " << modelName_ << ")" << std::endl;
}

inline void OllamaTranslator::stop() {
    running_.store(false);
    state_ = ModuleState::Stopped;
    
    std::cout << "[OllamaTranslator] Stopped (translated " << totalTranslated_ 
              << " blocks, cache hits: " << cacheHits_ << ")" << std::endl;
}

inline void OllamaTranslator::setSourceLanguage(const std::string& lang) {
    sourceLang_ = lang;
    std::cout << "[OllamaTranslator] Source language: " << lang << std::endl;
}

inline void OllamaTranslator::setTargetLanguage(const std::string& lang) {
    targetLang_ = lang;
    std::cout << "[OllamaTranslator] Target language: " << lang << std::endl;
}

inline void OllamaTranslator::setInferenceParams(const InferenceParams& params) {
    // Пока не используется - Ollama использует свои параметры
    (void)params;
}

inline void OllamaTranslator::setCacheSize(size_t size) {
    std::lock_guard lock(cacheMutex_);
    maxCacheSize_ = size;
    
    if (translationCache_.size() > maxCacheSize_) {
        translationCache_.resize(maxCacheSize_);
    }
}

inline void OllamaTranslator::setModel(const std::string& modelName) {
    modelName_ = modelName;
    std::cout << "[OllamaTranslator] Model: " << modelName_ << std::endl;
}

inline void OllamaTranslator::setOllamaUrl(const std::string& url) {
    ollamaUrl_ = url;
}

inline void OllamaTranslator::clearCache() {
    std::lock_guard lock(cacheMutex_);
    translationCache_.clear();
    std::cout << "[OllamaTranslator] Cache cleared" << std::endl;
}

inline bool OllamaTranslator::checkOllamaConnection() {
    if (!curl_) return false;
    
    std::string url = ollamaUrl_ + "/api/tags";
    
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 3L);
    
    CURLcode res = curl_easy_perform(curl_);
    long responseCode = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &responseCode);
    
    // Сбрасываем настройки
    curl_easy_reset(curl_);
    
    return (res == CURLE_OK && responseCode == 200);
}

inline std::string OllamaTranslator::getFromCache(const std::string& text) {
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

inline void OllamaTranslator::addToCache(const std::string& original, const std::string& translated) {
    std::lock_guard lock(cacheMutex_);
    
    if (translationCache_.size() >= maxCacheSize_) {
        // Удаляем самый старый
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

inline size_t OllamaTranslator::writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    std::string* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), realsize);
    return realsize;
}

inline std::string OllamaTranslator::callOllama(const std::string& prompt) {
    if (!curl_) return "";
    
    std::string url = ollamaUrl_ + "/api/generate";
    
    // Формируем JSON запрос
    std::string json = "{";
    json += "\"model\":\"" + modelName_ + "\",";
    json += "\"prompt\":\"" + prompt + "\",";
    json += "\"stream\":false";
    json += "}";
    
    std::string response;
    
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, json.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 10L);
    
    // Заголовки
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
    
    CURLcode res = curl_easy_perform(curl_);
    
    curl_slist_free_all(headers);
    curl_easy_reset(curl_);
    
    if (res != CURLE_OK) {
        std::cerr << "[OllamaTranslator] CURL error: " << curl_easy_strerror(res) << std::endl;
        return "";
    }
    
    // Парсим JSON ответ - ищем "response": "..."
    size_t pos = response.find("\"response\":\"");
    if (pos != std::string::npos) {
        pos += 11;
        size_t end = response.find("\"", pos);
        if (end != std::string::npos) {
            return response.substr(pos, end - pos);
        }
    }
    
    return "";
}

inline std::string OllamaTranslator::translateText(const std::string& text) {
    if (text.empty()) return "";
    
    // Проверяем кэш
    std::string cached = getFromCache(text);
    if (!cached.empty()) {
        return cached;
    }
    
    if (state_ != ModuleState::Running) {
        std::cerr << "[OllamaTranslator] Not running" << std::endl;
        return text;
    }
    
    // Формируем промпт для перевода
    std::string prompt = "Translate from " + sourceLang_ + " to " + targetLang_ + ":\n" + text;
    
    std::string translated = callOllama(prompt);
    
    if (!translated.empty()) {
        addToCache(text, translated);
        return translated;
    }
    
    return text;
}

inline TranslatedBlock OllamaTranslator::translate(const TextBlock& block) {
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

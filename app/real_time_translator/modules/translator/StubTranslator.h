#pragma once

#include "../../core/Interfaces.h"
#include "../../utils/ThreadPool.h"
#include <random>
#include <unordered_map>
#include <mutex>

namespace translator {

/**
 * @brief Stub-реализация переводчика - имитирует перевод с кэшированием
 */
class StubTranslator : public ITranslator {
private:
    std::string sourceLanguage_ = "auto";
    std::string targetLanguage_ = "ru";
    InferenceParams params_;
    size_t cacheSize_ = 1000;
    
    std::atomic<bool> running_{false};
    ModuleState state_ = ModuleState::Idle;
    std::string lastError_;
    
    // LRU кэш переводов
    std::unordered_map<std::string, std::string> translationCache_;
    std::mutex cacheMutex_;
    size_t cacheHits_ = 0;
    size_t cacheMisses_ = 0;
    
    // Для имитации задержки
    std::random_device rd_;
    std::mt19937 gen_;
    std::uniform_int_distribution<> delayDist_{50, 150};

public:
    StubTranslator() : gen_(rd_()) {}
    ~StubTranslator() override { stop(); }
    
    bool init() override {
        state_ = ModuleState::Idle;
        lastError_.clear();
        return true;
    }
    
    void start() override {
        running_.store(true);
        state_ = ModuleState::Running;
    }
    
    void stop() override {
        running_.store(false);
        state_ = ModuleState::Stopped;
    }
    
    ModuleState getState() const override { return state_; }
    std::string getLastError() const override { return lastError_; }
    
    void setSourceLanguage(const std::string& lang) override {
        sourceLanguage_ = lang;
    }
    
    void setTargetLanguage(const std::string& lang) override {
        targetLanguage_ = lang;
    }
    
    void setInferenceParams(const InferenceParams& params) override {
        params_ = params;
    }
    
    void setCacheSize(size_t size) override {
        cacheSize_ = size;
    }
    
    TranslatedBlock translate(const TextBlock& block) override {
        // Имитируем задержку перевода
        std::this_thread::sleep_for(std::chrono::milliseconds(delayDist_(gen_)));
        
        TranslatedBlock result;
        result.id = block.id;
        result.originalText = block.text;
        result.bbox = block.bbox;
        result.confidence = 0.9f;
        result.style = {};
        
        // Проверяем кэш
        {
            std::lock_guard lock(cacheMutex_);
            auto it = translationCache_.find(block.text);
            if (it != translationCache_.end()) {
                result.translatedText = it->second;
                cacheHits_++;
                return result;
            }
            cacheMisses_++;
        }
        
        // Имитация перевода (просто добавляем префикс)
        std::string translated;
        if (targetLanguage_ == "ru") {
            translated = "[RU] " + block.text;
        } else if (targetLanguage_ == "zh") {
            translated = "[ZH] " + block.text;
        } else if (targetLanguage_ == "de") {
            translated = "[DE] " + block.text;
        } else {
            translated = "[EN] " + block.text;
        }
        
        // Кладём в кэш
        {
            std::lock_guard lock(cacheMutex_);
            if (translationCache_.size() >= cacheSize_) {
                // Очищаем половину кэша
                auto it = translationCache_.begin();
                std::advance(it, translationCache_.size() / 2);
                translationCache_.erase(translationCache_.begin(), it);
            }
            translationCache_[block.text] = translated;
        }
        
        result.translatedText = translated;
        return result;
    }
    
    // Для отладки
    size_t getCacheHits() const { return cacheHits_; }
    size_t getCacheMisses() const { return cacheMisses_; }
};

} // namespace translator

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

// Python bindings for CTranslate2
#include <Python.h>

namespace translator {

/**
 * @brief Локальный переводчик на базе CTranslate2
 * 
 * Использует модель MarianMT (Helsinki-NLP) для перевода.
 * Требует Python с установленными: ctranslate2, transformers, sentencepiece
 */
class CTranslate2Translator : public ITranslator {
private:
    // Python объекты
    PyObject* translatorObj_ = nullptr;
    PyObject* translateFunc_ = nullptr;
    
    // Состояние
    ModuleState state_ = ModuleState::Idle;
    std::string lastError_;
    std::atomic<bool> running_{false};
    
    // Настройки
    std::string sourceLang_ = "en";
    std::string targetLang_ = "ru";
    std::string modelName_ = "Helsinki-NLP/opus-mt-en-ru";  // English -> Russian
    bool autoDetect_ = false;
    
    // Кэш переводов (LRU)
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

    // Inference параметры
    int maxLength_ = 512;
    int beamSize_ = 4;
    float temperature_ = 0.0f;

public:
    CTranslate2Translator();
    ~CTranslate2Translator() override;
    
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
    void enableCache(bool enable);
    void clearCache();
    void updateModel();
    
    std::vector<TranslatedBlock> translateBatch(const std::vector<TextBlock>& blocks);
    
    // Python initialization
    bool initPython();

private:
    std::string translateText(const std::string& text);
    std::string getFromCache(const std::string& text);
    void addToCache(const std::string& original, const std::string& translated);
};

// ============================================================================
// Реализация
// ============================================================================

inline CTranslate2Translator::CTranslate2Translator() {
    // Инициализация Python
    Py_Initialize();
}

inline CTranslate2Translator::~CTranslate2Translator() {
    stop();
    
    // Освобождаем Python объекты
    if (translatorObj_) {
        Py_DECREF(translatorObj_);
    }
    if (translateFunc_) {
        Py_DECREF(translateFunc_);
    }
}

inline bool CTranslate2Translator::initPython() {
    try {
        // Импортируем модуль переводчика
        PyObject* ct2Module = PyImport_ImportModule("ctranslate2");
        if (!ct2Module) {
            PyErr_Print();
            lastError_ = "Failed to import ctranslate2 module";
            return false;
        }
        
        // Получаем класс Translator
        PyObject* translatorClass = PyObject_GetAttrString(ct2Module, "Translator");
        if (!translatorClass) {
            PyErr_Print();
            lastError_ = "Failed to get Translator class";
            Py_DECREF(ct2Module);
            return false;
        }
        
        // Скачиваем модель если нужно и создаем translator
        std::cout << "[CTranslate2] Loading model: " << modelName_ << std::endl;
        
        PyObject* modelPath = PyUnicode_FromString(modelName_.c_str());
        translatorObj_ = PyObject_CallFunctionObjArgs(translatorClass, modelPath, NULL);
        Py_DECREF(modelPath);
        Py_DECREF(translatorClass);
        
        if (!translatorObj_) {
            PyErr_Print();
            lastError_ = "Failed to create translator (model may need download)";
            Py_DECREF(ct2Module);
            return false;
        }
        
        // Получаем функцию translate
        translateFunc_ = PyObject_GetAttrString(translatorObj_, "translate_batch");
        if (!translateFunc_) {
            PyErr_Print();
            lastError_ = "Failed to get translate_batch function";
            Py_DECREF(ct2Module);
            return false;
        }
        
        Py_DECREF(ct2Module);
        
        std::cout << "[CTranslate2] Translator initialized successfully" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

inline bool CTranslate2Translator::init() {
    try {
        state_ = ModuleState::Idle;
        lastError_.clear();
        
        // Инициализируем Python
        if (!Py_IsInitialized()) {
            Py_Initialize();
        }
        
        // Добавляем путь для импорта
        PyObject* sysPath = PySys_GetObject("path");
        PyList_Append(sysPath, PyUnicode_FromString("."));
        
        // Проверяем доступность ctranslate2
        PyObject* ct2Module = PyImport_ImportModule("ctranslate2");
        if (!ct2Module) {
            lastError_ = "ctranslate2 not installed. Run: pip install ctranslate2 transformers";
            std::cerr << "[CTranslate2] " << lastError_ << std::endl;
            return false;
        }
        Py_DECREF(ct2Module);
        
        // Выбираем модель в зависимости от направления перевода
        if (sourceLang_ == "en" && targetLang_ == "ru") {
            modelName_ = "Helsinki-NLP/opus-mt-en-ru";
        } else if (sourceLang_ == "ru" && targetLang_ == "en") {
            modelName_ = "Helsinki-NLP/opus-mt-ru-en";
        } else {
            // Для других языков используем мультиязычную модель
            modelName_ = "facebook/mbart-large-50-many-to-many";
        }
        
        return true;
        
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

inline void CTranslate2Translator::start() {
    if (running_.load()) return;
    
    running_.store(true);
    state_ = ModuleState::Running;
    
    // Инициализируем переводчик
    if (!initPython()) {
        state_ = ModuleState::Error;
        std::cerr << "[CTranslate2] Failed to initialize: " << lastError_ << std::endl;
    } else {
        std::cout << "[CTranslate2] Started" << std::endl;
    }
}

inline void CTranslate2Translator::stop() {
    running_.store(false);
    state_ = ModuleState::Stopped;
    
    std::cout << "[CTranslate2] Stopped (translated " << totalTranslated_ 
              << " blocks, cache hits: " << cacheHits_ << ")" << std::endl;
}

inline void CTranslate2Translator::setSourceLanguage(const std::string& lang) {
    sourceLang_ = lang;
    updateModel();
}

inline void CTranslate2Translator::setTargetLanguage(const std::string& lang) {
    targetLang_ = lang;
    updateModel();
}

inline void CTranslate2Translator::setInferenceParams(const InferenceParams& params) {
    maxLength_ = params.maxTokens > 0 ? params.maxTokens : 512;
    temperature_ = params.temperature;
}

inline void CTranslate2Translator::setModel(const std::string& modelName) {
    modelName_ = modelName;
}

inline void CTranslate2Translator::enableCache(bool enable) {
    if (!enable) {
        std::lock_guard lock(cacheMutex_);
        translationCache_.clear();
    }
}

inline void CTranslate2Translator::clearCache() {
    std::lock_guard lock(cacheMutex_);
    translationCache_.clear();
    std::cout << "[CTranslate2] Cache cleared" << std::endl;
}

inline void CTranslate2Translator::setCacheSize(size_t size) {
    std::lock_guard lock(cacheMutex_);
    maxCacheSize_ = size;
    
    // Обрезаем кэш если нужно
    if (translationCache_.size() > maxCacheSize_) {
        translationCache_.resize(maxCacheSize_);
    }
}

inline void CTranslate2Translator::updateModel() {
    // Обновляем модель в зависимости от языков
    if (sourceLang_ == "en" && targetLang_ == "ru") {
        modelName_ = "Helsinki-NLP/opus-mt-en-ru";
    } else if (sourceLang_ == "ru" && targetLang_ == "en") {
        modelName_ = "Helsinki-NLP/opus-mt-ru-en";
    } else if (sourceLang_ == "auto" && targetLang_ == "ru") {
        modelName_ = "Helsinki-NLP/opus-mt-mul-ru";
    } else {
        modelName_ = "Helsinki-NLP/opus-mt-multilingual";
    }
    
    std::cout << "[CTranslate2] Model updated: " << sourceLang_ << " -> " << targetLang_ 
              << " (" << modelName_ << ")" << std::endl;
}

inline std::string CTranslate2Translator::getFromCache(const std::string& text) {
    std::lock_guard lock(cacheMutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto oldest = now;
    size_t oldestIdx = 0;
    
    for (size_t i = 0; i < translationCache_.size(); i++) {
        if (translationCache_[i].original == text) {
            // Нашли в кэше - возвращаем и обновляем timestamp
            translationCache_[i].timestamp = now;
            cacheHits_++;
            return translationCache_[i].translated;
        }
        
        // Ищем самый старый для замены
        if (translationCache_[i].timestamp < oldest) {
            oldest = translationCache_[i].timestamp;
            oldestIdx = i;
        }
    }
    
    return "";
}

inline void CTranslate2Translator::addToCache(const std::string& original, const std::string& translated) {
    std::lock_guard lock(cacheMutex_);
    
    // Если кэш полный - заменяем самый старый
    if (translationCache_.size() >= maxCacheSize_) {
        translationCache_.erase(translationCache_.begin() + 0);
    }
    
    CacheEntry entry;
    entry.original = original;
    entry.translated = translated;
    entry.timestamp = std::chrono::steady_clock::now();
    translationCache_.push_back(entry);
}

inline std::string CTranslate2Translator::translateText(const std::string& text) {
    if (text.empty()) return "";
    
    // Проверяем кэш
    std::string cached = getFromCache(text);
    if (!cached.empty()) {
        return cached;
    }
    
    if (!translatorObj_ || !translateFunc_) {
        std::cerr << "[CTranslate2] Translator not initialized" << std::endl;
        return text;
    }
    
    try {
        // Создаем список входных текстов
        PyObject* inputList = PyList_New(1);
        PyList_SetItem(inputList, 0, PyUnicode_FromString(text.c_str()));
        
        // Вызываем translate_batch
        PyObject* result = PyObject_CallFunctionObjArgs(translateFunc_, inputList, NULL);
        Py_DECREF(inputList);
        
        if (!result) {
            PyErr_Print();
            return text;
        }
        
        // Извлекаем переведенный текст из результата
        std::string translated;
        
        if (PyList_Check(result) && PyList_Size(result) > 0) {
            PyObject* first = PyList_GetItem(result, 0);
            
            if (PyList_Check(first) && PyList_Size(first) > 0) {
                PyObject* translatedText = PyList_GetItem(first, 0);
                const char* str = PyUnicode_AsUTF8(translatedText);
                if (str) {
                    translated = str;
                }
            }
        }
        
        Py_DECREF(result);
        
        if (!translated.empty()) {
            addToCache(text, translated);
            return translated;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[CTranslate2] Translation error: " << e.what() << std::endl;
    }
    
    return text;
}

inline TranslatedBlock CTranslate2Translator::translate(const TextBlock& block) {
    TranslatedBlock result;
    result.id = block.id;
    result.originalText = block.text;
    result.confidence = block.confidence;
    
    if (state_ != ModuleState::Running) {
        result.translatedText = "[Translator not ready] " + block.text;
        return result;
    }
    
    // Переводим текст
    result.translatedText = translateText(block.text);
    
    // Если перевод не удался, используем оригинал
    if (result.translatedText.empty() || result.translatedText == block.text) {
        result.translatedText = "[Translation failed] " + block.text;
    }
    
    totalTranslated_++;
    return result;
}

inline std::vector<TranslatedBlock> CTranslate2Translator::translateBatch(const std::vector<TextBlock>& blocks) {
    std::vector<TranslatedBlock> results;
    
    if (state_ != ModuleState::Running) {
        std::cerr << "[CTranslate2] Not running, cannot translate" << std::endl;
        return results;
    }
    
    for (const auto& block : blocks) {
        results.push_back(translate(block));
    }
    
    return results;
}

} // namespace translator

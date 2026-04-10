#pragma once

#include "Interfaces.h"
#include "Logger.h"

#include <string>
#include <filesystem>
#include <functional>
#include <optional>

// Forward declaration - будет подключен при наличии
#if __has_include(<nlohmann/json.hpp>)
#include <nlohmann/json.hpp>
#define HAS_NLOHMANN_JSON 1
#else
#define HAS_NLOHMANN_JSON 0
#endif

namespace translator {

/**
 * @brief Менеджер конфигурации
 */
class ConfigManager {
private:
#if HAS_NLOHMANN_JSON
    nlohmann::json config_;
#endif
    std::filesystem::path configPath_;
    std::function<void()> onChangeCallback_;

public:
    ConfigManager();
    ~ConfigManager();

    // Запрет копирования
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    /**
     * @brief Загрузить конфигурацию из файла
     * @param path путь к файлу
     * @return true при успехе
     */
    bool load(const std::filesystem::path& path);

    /**
     * @brief Сохранить конфигурацию в файл
     * @param path путь к файлу (опционально)
     * @return true при успехе
     */
    bool save(const std::filesystem::path& path = {});

    /**
     * @brief Получить значение по ключу
     * @tparam T тип значения
     * @param key ключ (поддерживает вложенные через точку)
     * @return значение или nullopt
     */
    template<typename T>
    std::optional<T> get(const std::string& key) const;

    /**
     * @brief Установить значение
     * @tparam T тип значения
     * @param key ключ
     * @param value значение
     */
    template<typename T>
    void set(const std::string& key, const T& value);

    /**
     * @brief Проверить наличие ключа
     * @param key ключ
     * @return true если ключ существует
     */
    bool has(const std::string& key) const;

    /**
     * @brief Получить весь конфиг как JSON строку
     * @return JSON строка
     */
    std::string toString() const;

    /**
     * @brief Подписаться на изменения конфигурации
     * @param callback функция обратного вызова
     */
    void subscribe(std::function<void()> callback);

    /**
     * @brief Перезагрузить конфигурацию из файла
     * @return true при успехе
     */
    bool reload();
};

// Реализация шаблонных методов
#if HAS_NLOHMANN_JSON

template<typename T>
std::optional<T> ConfigManager::get(const std::string& key) const {
    try {
        nlohmann::json::json_pointer ptr(key);
        if (config_.contains(ptr)) {
            return config_.at(ptr).get<T>();
        }
    } catch (...) {
        // Игнорируем ошибки парсинга
    }
    return std::nullopt;
}

template<typename T>
void ConfigManager::set(const std::string& key, const T& value) {
    try {
        nlohmann::json::json_pointer ptr(key);
        config_[ptr] = value;
        
        if (onChangeCallback_) {
            onChangeCallback_();
        }
    } catch (...) {
        // Игнорируем ошибки
    }
}

#else

// Заглушки без nlohmann
template<typename T>
std::optional<T> ConfigManager::get(const std::string&) const {
    return std::nullopt;
}

template<typename T>
void ConfigManager::set(const std::string&, const T&) {
    // Ничего не делаем
}

#endif

// ============================================================================
// Конфигурация приложения
// ============================================================================

struct CaptureConfig {
    std::string backend = "dxgi";
    CaptureSource source;
    CaptureMode mode = CaptureMode::Hybrid;
    int timerIntervalMs = 16;  // ~60 FPS
    std::vector<Rect> ignoreRegions;
    PixelFormat format = PixelFormat::RGBA;
};

struct OCRConfig {
    std::string engine = "tesseract";
    std::vector<std::string> languages = {"eng", "rus"};
    std::vector<std::string> preprocessing;
    bool textTracking = true;
    float minTextSize = 8.0f;
};

struct TranslatorConfig {
    std::string modelPath;
    std::string sourceLanguage = "auto";
    std::string targetLanguage = "ru";
    size_t cacheSize = 1000;
    int threads = 4;
    InferenceParams params;
};

struct OverlayConfig {
    BackgroundMode bgMode = BackgroundMode::Fixed;
    Color bgColor = {0, 0, 0, 180};
    float bgAlpha = 180.0f;
    Style textStyle;
    bool autoFontMatch = false;
    bool separateWindow = false;
    std::vector<Hotkey> hotkeys;
};

struct GeneralConfig {
    bool startMinimized = false;
    bool autoStart = false;
    LogLevel logLevel = LogLevel::Info;
    std::filesystem::path logFile;
};

struct AppConfig {
    CaptureConfig capture;
    OCRConfig ocr;
    TranslatorConfig translator;
    OverlayConfig overlay;
    GeneralConfig general;
};

} // namespace translator

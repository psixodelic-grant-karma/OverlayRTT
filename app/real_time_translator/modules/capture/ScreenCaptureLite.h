#pragma once

#include "../../core/Interfaces.h"
#include <ScreenCapture.h>

#include <thread>
#include <atomic>
#include <memory>
#include <functional>
#include <mutex>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iostream>
#include <leptonica/allheaders.h>

namespace translator {

/**
 * @brief Захват экрана через screen_capture_lite
 * 
 * Кроссплатформенная библиотека с высокой производительностью.
 * Поддерживает onFrameChanged - захват только при изменении экрана.
 */
class ScreenCaptureLite : public ICapture {
private:
    CaptureSource source_;
    CaptureMode mode_ = CaptureMode::Hybrid;
    std::vector<Rect> ignoreRegions_;
    std::function<void(const Frame&)> callback_;
    
    std::atomic<bool> running_{false};
    std::shared_ptr<SL::Screen_Capture::IScreenCaptureManager> captureManager_;
    
    ModuleState state_ = ModuleState::Idle;
    std::string lastError_;
    
    // Текущий захваченный кадр
    Frame currentFrame_;
    std::mutex frameMutex_;
    
    // Для режима onFrameChanged
    bool useFrameChanged_ = true;
    int frameChangeIntervalMs_ = 33; // ~30 FPS

    // Для отладки - путь для сохранения кадров
    std::string debugCapturePath_ = "/tmp/translator_capture";
    bool saveDebugImages_ = false;
    
    void saveDebugImage(const uint8_t* data, int width, int height, const std::string& suffix);

public:
    ScreenCaptureLite() = default;
    ~ScreenCaptureLite() override;
    
    bool init() override;
    void start() override;
    void stop() override;
    
    ModuleState getState() const override { return state_; }
    std::string getLastError() const override { return lastError_; }
    
    bool setCaptureSource(const CaptureSource& source) override;
    bool setIgnoreRegions(const std::vector<Rect>& regions) override;
    void setCaptureMode(CaptureMode mode) override;
    
    Frame getLatestFrame() override;
    void setOnFrameCallback(std::function<void(const Frame&)> callback) override;
    
    /**
     * @brief Включить/выключить режим захвата только изменений
     * @param enable true - только при изменении, false - каждый кадр
     */
    void setFrameChangedMode(bool enable);
    
    /**
     * @brief Установить интервал захвата в режиме onNewFrame
     * @param intervalMs интервал в миллисекундах
     */
    void setFrameChangeInterval(int intervalMs);

    /**
     * @brief Включить режим отладки (сохранение изображений)
     * @param enable включить/выключить
     * @param path путь для сохранения файлов
     */
    void setDebugMode(bool enable, const std::string& path = "/tmp/translator") override;

private:
    void setupCapture();
    void onNewFrame(const SL::Screen_Capture::Image& img, const SL::Screen_Capture::Monitor& monitor);
    void onFrameChanged(const SL::Screen_Capture::Image& img, const SL::Screen_Capture::Monitor& monitor);
    Frame convertImageToFrame(const SL::Screen_Capture::Image& img);
};

// ============================================================================
// Реализация
// ============================================================================

inline ScreenCaptureLite::~ScreenCaptureLite() {
    stop();
}

inline bool ScreenCaptureLite::init() {
    try {
        state_ = ModuleState::Idle;
        lastError_.clear();
        
        // Проверяем доступные мониторы
        auto monitors = SL::Screen_Capture::GetMonitors();
        if (monitors.empty()) {
            lastError_ = "No monitors found";
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

inline void ScreenCaptureLite::start() {
    if (running_.load()) return;
    
    running_.store(true);
    state_ = ModuleState::Running;
    
    setupCapture();
}

inline void ScreenCaptureLite::stop() {
    running_.store(false);
    
    if (captureManager_) {
        captureManager_.reset();
    }
    
    state_ = ModuleState::Stopped;
}

inline bool ScreenCaptureLite::setCaptureSource(const CaptureSource& source) {
    source_ = source;
    return true;
}

inline bool ScreenCaptureLite::setIgnoreRegions(const std::vector<Rect>& regions) {
    ignoreRegions_ = regions;
    return true;
}

inline void ScreenCaptureLite::setCaptureMode(CaptureMode mode) {
    mode_ = mode;
    
    // Hybrid = используем onFrameChanged для оптимизации
    useFrameChanged_ = (mode == CaptureMode::Hybrid || mode == CaptureMode::Callback);
}

inline Frame ScreenCaptureLite::getLatestFrame() {
    std::lock_guard lock(frameMutex_);
    return currentFrame_;
}

inline void ScreenCaptureLite::setOnFrameCallback(std::function<void(const Frame&)> callback) {
    callback_ = callback;
}

inline void ScreenCaptureLite::setFrameChangedMode(bool enable) {
    useFrameChanged_ = enable;
}

inline void ScreenCaptureLite::setFrameChangeInterval(int intervalMs) {
    frameChangeIntervalMs_ = intervalMs;
}

inline void ScreenCaptureLite::setupCapture() {
    try {
        auto monitors = SL::Screen_Capture::GetMonitors();
        
        std::cout << "[ScreenCaptureLite] Found " << monitors.size() << " monitors" << std::endl;
        
        if (monitors.empty()) {
            lastError_ = "No monitors available";
            return;
        }
        
        // Выбираем первый монитор
        SL::Screen_Capture::Monitor targetMonitor = monitors[0];
        
        std::cout << "[ScreenCaptureLite] Using monitor: " 
                  << SL::Screen_Capture::Width(targetMonitor) << "x"
                  << SL::Screen_Capture::Height(targetMonitor) << std::endl;
        
        // Создаём конфигурацию захвата
        auto config = SL::Screen_Capture::CreateCaptureConfiguration([targetMonitor]() {
            std::vector<SL::Screen_Capture::Monitor> result;
            result.push_back(targetMonitor);
            return result;
        });
        
        // Важно: нужно установить монитор, иначе будет чёрный экран
        // Используем тот же монитор что и в GetMonitors()
        
        if (useFrameChanged_) {
            std::cout << "[ScreenCaptureLite] Using onFrameChanged mode" << std::endl;
            // Режим onFrameChanged - только при изменении экрана (ОПТИМАЛЬНО для OCR!)
            config->onFrameChanged([this](const SL::Screen_Capture::Image& img, 
                                          const SL::Screen_Capture::Monitor& monitor) {
                onFrameChanged(img, monitor);
            });
        } else {
            std::cout << "[ScreenCaptureLite] Using onNewFrame mode" << std::endl;
            // Режим onNewFrame - каждый кадр с заданным интервалом
            config->onNewFrame([this](const SL::Screen_Capture::Image& img, 
                                       const SL::Screen_Capture::Monitor& monitor) {
                onNewFrame(img, monitor);
            });
        }
        
        // Запускаем захват (возвращает shared_ptr)
        captureManager_ = config->start_capturing();
        
        if (captureManager_) {
            // Устанавливаем интервал для onNewFrame режима
            captureManager_->setFrameChangeInterval(std::chrono::milliseconds(frameChangeIntervalMs_));
        }
        
        std::cout << "[ScreenCaptureLite] captureManager_: " << (captureManager_ ? "OK" : "NULL") << std::endl;
        
        if (!captureManager_) {
            lastError_ = "Failed to start capture";
            state_ = ModuleState::Error;
        }
        
    } catch (const std::exception& e) {
        std::cout << "[ScreenCaptureLite] Exception: " << e.what() << std::endl;
        lastError_ = e.what();
        state_ = ModuleState::Error;
    }
}

inline void ScreenCaptureLite::onNewFrame(const SL::Screen_Capture::Image& img, 
                                           const SL::Screen_Capture::Monitor& monitor) {
    (void)monitor;
    if (!running_.load()) return;
    
    // Проверим данные
    auto src = SL::Screen_Capture::StartSrc(img);
    if (src) {
        // Проверим несколько пикселей
        int sum = 0;
        for (int i = 0; i < 1000; i++) {
            sum += src[i].R + src[i].G + src[i].B;
        }
        std::cout << "[ScreenCaptureLite] onNewFrame: " 
                  << SL::Screen_Capture::Width(img) << "x" 
                  << SL::Screen_Capture::Height(img)
                  << " avg pixel sum=" << (sum/1000) << std::endl;
    }
    
    auto frame = convertImageToFrame(img);
    
    {
        std::lock_guard lock(frameMutex_);
        currentFrame_ = frame;
    }
    
    if (callback_) {
        callback_(frame);
    }
}

inline void ScreenCaptureLite::onFrameChanged(const SL::Screen_Capture::Image& img, 
                                               const SL::Screen_Capture::Monitor& monitor) {
    (void)monitor;
    if (!running_.load()) return;
    
    auto frame = convertImageToFrame(img);
    
    {
        std::lock_guard lock(frameMutex_);
        currentFrame_ = frame;
    }
    
    if (callback_) {
        callback_(frame);
    }
}

inline Frame ScreenCaptureLite::convertImageToFrame(const SL::Screen_Capture::Image& img) {
    Frame frame;
    
    // Используем функции Width() и Height() из библиотеки
    frame.width = SL::Screen_Capture::Width(img);
    frame.height = SL::Screen_Capture::Height(img);
    frame.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    
    frame.sourceRect.x = 0;
    frame.sourceRect.y = 0;
    frame.sourceRect.width = frame.width;
    frame.sourceRect.height = frame.height;
    
    // Копируем данные изображения
    // screen_capture_lite возвращает BGRA данные
    size_t rowSize = static_cast<size_t>(frame.width) * 4;
    size_t dataSize = rowSize * static_cast<size_t>(frame.height);
    frame.buffer.resize(dataSize);
    
    // StartSrc() возвращает указатель на данные
    auto src = SL::Screen_Capture::StartSrc(img);
    if (src) {
        // Проверяем, контигуальны ли данные (без padding)
        if (SL::Screen_Capture::isDataContiguous(img)) {
            // Просто копируем всё
            std::memcpy(frame.buffer.data(), src, dataSize);
        } else {
            // Копируем построчно (есть padding между строками)
            auto srcRow = src;
            auto dstRow = frame.buffer.data();
            for (int y = 0; y < frame.height; y++) {
                std::memcpy(dstRow, srcRow, rowSize);
                srcRow = SL::Screen_Capture::GotoNextRow(img, srcRow);
                dstRow += rowSize;
            }
        }
    }
    
    // Устанавливаем формат
    frame.format = PixelFormat::BGRA;
    
    // Сохраняем кадр для отладки
    if (saveDebugImages_) {
        saveDebugImage(frame.buffer.data(), frame.width, frame.height, "capture");
    }
    
    return frame;
}

inline void ScreenCaptureLite::setDebugMode(bool enable, const std::string& path) {
    saveDebugImages_ = enable;
    debugCapturePath_ = path;
}

// ============================================================================
// Отладочные функции
// ============================================================================

inline void ScreenCaptureLite::saveDebugImage(const uint8_t* bgraData, int width, int height, const std::string& suffix) {
    if (!bgraData || width <= 0 || height <= 0) return;
    
    static int imageCounter = 0;
    static std::mutex counterMutex;
    
    int counter;
    {
        std::lock_guard lock(counterMutex);
        counter = imageCounter++;
    }
    
    // Создаём Pix из BGRA данных
    // Leptonica использует 32-битный формат: (R<<16)|(G<<8)|B
    Pix* pix = pixCreate(width, height, 32);
    l_uint32* data = pixGetData(pix);
    int wpl = pixGetWpl(pix);
    
    for (int y = 0; y < height; y++) {
        l_uint32* line = data + y * wpl;
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 4;
            // BGRA -> RGB в формате Leptonica
            l_uint32 pixel = 
                (static_cast<l_uint32>(bgraData[idx + 2]) << 16) |  // R
                (static_cast<l_uint32>(bgraData[idx + 1]) << 8)  |  // G
                static_cast<l_uint32>(bgraData[idx + 0]);           // B
            line[x] = pixel;
        }
    }
    
    // Сохраняем как PNG
    std::ostringstream oss;
    oss << debugCapturePath_ << "_" << suffix << "_" << counter << ".png";
    std::string filepath = oss.str();
    
    pixWrite(filepath.c_str(), pix, IFF_PNG);
    pixDestroy(&pix);
}

} // namespace translator

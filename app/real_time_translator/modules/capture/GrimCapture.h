#pragma once

#include "../../core/Interfaces.h"
#include <leptonica/allheaders.h>

#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <cstring>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstdlib>

namespace translator {

/**
 * @brief Захват экрана через grim (Wayland/Hyprland)
 * 
 * Нативный захват для Wayland окружения
 */
class GrimCapture : public ICapture {
private:
    // Состояние
    ModuleState state_ = ModuleState::Idle;
    std::string lastError_;
    std::atomic<bool> running_{false};
    
    // Текущий кадр
    Frame currentFrame_;
    std::mutex frameMutex_;
    
    // Настройки
    int width_ = 0;
    int height_ = 0;
    int captureIntervalMs_ = 1000;  // 1 FPS по умолчанию
    
    // Для отладки
    bool saveDebugImages_ = false;
    std::string debugPath_ = "/tmp/translator";
    int debugCounter_ = 0;
    std::mutex debugMutex_;
    
    // Поток захвата
    std::thread captureThread_;

public:
    GrimCapture() = default;
    
    ~GrimCapture() override {
        stop();
    }
    
    bool init() override {
        try {
            state_ = ModuleState::Idle;
            lastError_.clear();
            
            // Читаем DISPLAY для отладки
            const char* displayEnv = std::getenv("DISPLAY");
            const char* waylandEnv = std::getenv("WAYLAND_DISPLAY");
            
            std::cout << "[GrimCapture] DISPLAY=" << (displayEnv ? displayEnv : "null")
                      << " WAYLAND_DISPLAY=" << (waylandEnv ? waylandEnv : "null") << std::endl;
            
            // Проверяем доступность grim
            int result = std::system("which grim > /dev/null 2>&1");
            if (result != 0) {
                lastError_ = "grim not found";
                return false;
            }
            
            return true;
        } catch (const std::exception& e) {
            lastError_ = e.what();
            return false;
        }
    }
    
    void start() override {
        if (running_.load()) return;
        
        running_.store(true);
        state_ = ModuleState::Running;
        
        captureThread_ = std::thread(&GrimCapture::captureLoop, this);
    }
    
    void stop() override {
        running_.store(false);
        
        if (captureThread_.joinable()) {
            captureThread_.join();
        }
        
        state_ = ModuleState::Stopped;
    }
    
    ModuleState getState() const override { return state_; }
    std::string getLastError() const override { return lastError_; }
    
    bool setCaptureSource(const CaptureSource& source) override {
        (void)source;
        return true;
    }
    
    bool setIgnoreRegions(const std::vector<Rect>& regions) override {
        (void)regions;
        return true;
    }
    
    void setCaptureMode(CaptureMode mode) override {
        (void)mode;
    }
    
    void setCaptureInterval(int intervalMs) {
        captureIntervalMs_ = intervalMs;
    }
    
    Frame getLatestFrame() override {
        std::lock_guard lock(frameMutex_);
        return currentFrame_;
    }
    
    void setOnFrameCallback(std::function<void(const Frame&)> callback) override {
        (void)callback;
    }
    
    void setDebugMode(bool enable, const std::string& path = "/tmp/translator") override {
        saveDebugImages_ = enable;
        debugPath_ = path;
    }

private:
    void captureLoop() {
        std::cout << "[GrimCapture] Capture loop started (interval=" << captureIntervalMs_ << "ms)" << std::endl;
        
        // Временный файл для скриншота
        std::string tmpPath = "/tmp/grim_capture.png";
        
        while (running_.load()) {
            auto startTime = std::chrono::steady_clock::now();
            
            // Запускаем grim (без -o для использования основного монитора)
            std::string cmd = "grim " + tmpPath + " 2>/dev/null";
            int result = std::system(cmd.c_str());
            
            if (result == 0) {
                // Читаем PNG файл
                Frame frame = loadPngFrame(tmpPath);
                
                if (!frame.empty()) {
                    std::lock_guard lock(frameMutex_);
                    currentFrame_ = frame;
                    
                    if (saveDebugImages_) {
                        saveDebugImage(frame);
                    }
                }
            } else {
                std::cerr << "[GrimCapture] grim failed with code " << result << std::endl;
            }
            
            // Ждем до следующего кадра
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            auto remaining = std::chrono::milliseconds(captureIntervalMs_) - elapsed;
            if (remaining.count() > 0) {
                std::this_thread::sleep_for(remaining);
            }
        }
        
        std::cout << "[GrimCapture] Capture loop stopped" << std::endl;
    }
    
    Frame loadPngFrame(const std::string& filepath) {
        Frame frame;
        
        // Читаем PNG через Leptonica
        Pix* pix = pixRead(filepath.c_str());
        if (!pix) {
            std::cerr << "[GrimCapture] Failed to read PNG: " << filepath << std::endl;
            return frame;
        }
        
        frame.width = pixGetWidth(pix);
        frame.height = pixGetHeight(pix);
        frame.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
        
        frame.sourceRect = {0, 0, frame.width, frame.height};
        frame.format = PixelFormat::BGRA;
        
        // Конвертируем в BGRA
        // Leptonica может хранить в разных форматах, приводим к 32 бит
        Pix* pix32 = pixConvertTo32(pix);
        if (!pix32) {
            pixDestroy(&pix);
            return frame;
        }
        
        // Копируем данные
        size_t dataSize = static_cast<size_t>(frame.width) * frame.height * 4;
        frame.buffer.resize(dataSize);
        
        l_uint32* data = pixGetData(pix32);
        int wpl = pixGetWpl(pix32);
        
        uint8_t* dst = frame.buffer.data();
        for (int y = 0; y < frame.height; y++) {
            l_uint32* line = data + y * wpl;
            for (int x = 0; x < frame.width; x++) {
                int idx = (y * frame.width + x) * 4;
                l_uint32 pixel = line[x];
                // Leptonica 32-bit: (R<<24)|(G<<16)|(B<<8)|A (little-endian)
                // В памяти: B G R A (BGRA)
                dst[idx + 0] = static_cast<uint8_t>((pixel >> 24) & 0xFF);  // A
                dst[idx + 1] = static_cast<uint8_t>((pixel >> 16) & 0xFF);  // R
                dst[idx + 2] = static_cast<uint8_t>((pixel >> 8) & 0xFF);   // G  
                dst[idx + 3] = static_cast<uint8_t>(pixel & 0xFF);          // B
            }
        }
        
        pixDestroy(&pix32);
        pixDestroy(&pix);
        
        // Удаляем временный файл
        std::remove(filepath.c_str());
        
        return frame;
    }
    
    void saveDebugImage(const Frame& frame) {
        if (frame.empty()) return;
        
        int counter;
        {
            std::lock_guard lock(debugMutex_);
            counter = debugCounter_++;
        }
        
        Pix* pix = pixCreate(frame.width, frame.height, 32);
        l_uint32* data = pixGetData(pix);
        int wpl = pixGetWpl(pix);
        
        const uint8_t* src = frame.buffer.data();
        for (int y = 0; y < frame.height; y++) {
            l_uint32* line = data + y * wpl;
            for (int x = 0; x < frame.width; x++) {
                int idx = (y * frame.width + x) * 4;
                l_uint32 pixel = 
                    (static_cast<l_uint32>(src[idx + 0]) << 24) |
                    (static_cast<l_uint32>(src[idx + 1]) << 16) |
                    static_cast<l_uint32>(src[idx + 2] << 8);
                line[x] = pixel;
            }
        }
        
        std::ostringstream oss;
        oss << debugPath_ << "_capture_" << counter << ".png";
        pixWrite(oss.str().c_str(), pix, IFF_PNG);
        pixDestroy(&pix);
    }
};

} // namespace translator

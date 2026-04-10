#pragma once

#include "../../core/Interfaces.h"
#include <thread>
#include <atomic>

namespace translator {

/**
 * @brief Stub-реализация захвата - генерирует тестовые кадры
 */
class NullCapture : public ICapture {
private:
    CaptureSource source_;
    CaptureMode mode_ = CaptureMode::Timer;
    std::function<void(const Frame&)> callback_;
    std::atomic<bool> running_{false};
    std::thread captureThread_;
    ModuleState state_ = ModuleState::Idle;
    std::string lastError_;
    
    int width_ = 1920;
    int height_ = 1080;
    int frameCount_ = 0;

public:
    NullCapture() = default;
    ~NullCapture() override { stop(); }
    
    bool init() override {
        state_ = ModuleState::Idle;
        lastError_.clear();
        return true;
    }
    
    void start() override {
        if (running_.load()) return;
        
        running_.store(true);
        state_ = ModuleState::Running;
        
        // Запускаем поток захвата
        captureThread_ = std::thread([this]() {
            while (running_.load()) {
                Frame frame;
                frame.width = width_;
                frame.height = height_;
                frame.format = PixelFormat::RGBA;
                frame.timestamp = ++frameCount_;
                frame.sourceRect = {0, 0, width_, height_};
                
                // Создаём тестовый буфер (зеленоватый прямоугольник)
                frame.buffer.resize(width_ * height_ * 4);
                for (int y = 0; y < height_; ++y) {
                    for (int x = 0; x < width_; ++x) {
                        int idx = (y * width_ + x) * 4;
                        frame.buffer[idx + 0] = 50;          // R
                        frame.buffer[idx + 1] = 150 + (y * 100 / height_); // G
                        frame.buffer[idx + 2] = 50;          // B
                        frame.buffer[idx + 3] = 255;         // A
                    }
                }
                
                if (callback_) {
                    callback_(frame);
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 FPS
            }
        });
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
        source_ = source;
        if (source.type == CaptureSource::Region) {
            width_ = source.regionRect.width;
            height_ = source.regionRect.height;
        }
        return true;
    }
    
    bool setIgnoreRegions(const std::vector<Rect>&) override { return true; }
    void setCaptureMode(CaptureMode mode) override { mode_ = mode; }
    
    Frame getLatestFrame() override {
        // Stub - возвращаем пустой кадр
        return Frame{};
    }
    
    void setOnFrameCallback(std::function<void(const Frame&)> callback) override {
        callback_ = callback;
    }
};

} // namespace translator

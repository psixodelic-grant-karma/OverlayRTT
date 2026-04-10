#pragma once

#include "../../core/Interfaces.h"
#include <random>
#include <sstream>

namespace translator {

/**
 * @brief Stub-реализация OCR - возвращает тестовые текстовые блоки
 */
class StubOCR : public IOCR {
private:
    std::vector<std::string> languages_;
    std::vector<std::string> filters_;
    bool textTracking_ = true;
    std::atomic<bool> running_{false};
    ModuleState state_ = ModuleState::Idle;
    std::string lastError_;
    
    // Для имитации распознавания
    std::random_device rd_;
    std::mt19937 gen_;
    std::uniform_int_distribution<> delayDist_{30, 80};
    
    // Храним последние блоки для text tracking
    std::vector<TextBlock> lastBlocks_;
    int blockIdCounter_ = 0;

public:
    StubOCR() : gen_(rd_()) {}
    ~StubOCR() override { stop(); }
    
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
    
    void setLanguages(const std::vector<std::string>& langs) override {
        languages_ = langs;
    }
    
    void setPreprocessingFilters(const std::vector<std::string>& filters) override {
        filters_ = filters;
    }
    
    void enableTextTracking(bool enable) override {
        textTracking_ = enable;
    }
    
    std::vector<TextBlock> processFrame(const Frame& frame) override {
        // Имитируем задержку OCR
        std::this_thread::sleep_for(std::chrono::milliseconds(delayDist_(gen_)));
        
        // Генерируем тестовые блоки
        std::vector<TextBlock> blocks;
        
        // Блок 1 - заголовок
        TextBlock block1;
        block1.id = ++blockIdCounter_;
        block1.text = "Hello World";
        block1.bbox = {100, 100, 200, 30};
        block1.background = {50, 50, 50, 255};
        block1.font = {"Arial", 16, true, false, {255, 255, 255, 255}};
        block1.confidence = 0.95f;
        blocks.push_back(block1);
        
        // Блок 2 - кнопка
        TextBlock block2;
        block2.id = ++blockIdCounter_;
        block2.text = "Click here";
        block2.bbox = {100, 200, 150, 40};
        block2.background = {0, 100, 200, 255};
        block2.font = {"Arial", 14, false, false, {255, 255, 255, 255}};
        block2.confidence = 0.92f;
        blocks.push_back(block2);
        
        // Блок 3 - текст
        TextBlock block3;
        block3.id = ++blockIdCounter_;
        block3.text = "This is a sample text for translation testing";
        block3.bbox = {100, 300, 400, 60};
        block3.background = {255, 255, 255, 255};
        block3.font = {"Arial", 12, false, false, {0, 0, 0, 255}};
        block3.confidence = 0.88f;
        blocks.push_back(block3);
        
        return blocks;
    }
};

} // namespace translator

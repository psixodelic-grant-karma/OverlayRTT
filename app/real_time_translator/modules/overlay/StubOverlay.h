#pragma once

#include "../../core/Interfaces.h"
#include <iostream>
#include <chrono>

namespace translator {

/**
 * @brief Stub-реализация оверлея - выводит в консоль
 */
class StubOverlay : public IOverlay {
private:
    BackgroundMode bgMode_ = BackgroundMode::Fixed;
    Style textStyle_;
    bool autoFontMatch_ = false;
    bool visible_ = true;
    bool separateWindow_ = false;
    std::vector<Hotkey> hotkeys_;
    
    std::atomic<bool> running_{false};
    ModuleState state_ = ModuleState::Idle;
    std::string lastError_;
    
    int renderCount_ = 0;

public:
    StubOverlay() = default;
    ~StubOverlay() override { stop(); }
    
    bool init() override {
        state_ = ModuleState::Idle;
        lastError_.clear();
        return true;
    }
    
    void start() override {
        running_.store(true);
        state_ = ModuleState::Running;
        std::cout << "[StubOverlay] Started" << std::endl;
    }
    
    void stop() override {
        running_.store(false);
        state_ = ModuleState::Stopped;
        std::cout << "[StubOverlay] Stopped" << std::endl;
    }
    
    ModuleState getState() const override { return state_; }
    std::string getLastError() const override { return lastError_; }
    
    void setBackgroundMode(BackgroundMode mode) override {
        bgMode_ = mode;
    }
    
    void setTextStyle(const Style& style) override {
        textStyle_ = style;
    }
    
    void setAutoFontMatch(bool enable) override {
        autoFontMatch_ = enable;
    }
    
    void render(const std::vector<TranslatedBlock>& blocks, const Frame& originalFrame) override {
        if (!visible_ || blocks.empty()) return;
        
        renderCount_++;
        
        // Выводим информацию о блоках (в реальном приложении здесь был бы рендеринг)
        if (renderCount_ % 30 == 0) { // Раз в ~1 секунду
            std::cout << "[StubOverlay] Rendering " << blocks.size() << " blocks (frame #" 
                      << renderCount_ << ")" << std::endl;
            
            for (size_t i = 0; i < std::min(blocks.size(), size_t(3)); ++i) {
                const auto& block = blocks[i];
                std::cout << "  Block " << block.id << ": \"" 
                          << block.originalText << "\" -> \"" 
                          << block.translatedText << "\"" << std::endl;
            }
            
            if (blocks.size() > 3) {
                std::cout << "  ... and " << (blocks.size() - 3) << " more" << std::endl;
            }
        }
    }
    
    void setHotkeys(const std::vector<Hotkey>& keys) override {
        hotkeys_ = keys;
    }
    
    void setVisible(bool visible) override {
        visible_ = visible;
        std::cout << "[StubOverlay] Visible: " << (visible ? "true" : "false") << std::endl;
    }
    
    void setOutputWindow(bool useSeparateWindow) override {
        separateWindow_ = useSeparateWindow;
    }
    
    int getRenderCount() const { return renderCount_; }
};

} // namespace translator

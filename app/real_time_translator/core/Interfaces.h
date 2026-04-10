#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include <optional>

namespace translator {

// ============================================================================
// Основные структуры данных
// ============================================================================

struct Rect {
    int x = 0, y = 0, width = 0, height = 0;
    
    bool empty() const { return width <= 0 || height <= 0; }
    bool contains(int px, int py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
};

struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;
    
    static Color fromARGB(uint32_t argb) {
        return Color{
            static_cast<uint8_t>((argb >> 16) & 0xFF),
            static_cast<uint8_t>((argb >> 8) & 0xFF),
            static_cast<uint8_t>(argb & 0xFF),
            static_cast<uint8_t>((argb >> 24) & 0xFF)
        };
    }
    uint32_t toARGB() const {
        return (static_cast<uint32_t>(a) << 24) | 
               (static_cast<uint32_t>(r) << 16) | 
               (static_cast<uint32_t>(g) << 8) | 
               static_cast<uint32_t>(b);
    }
};

struct FontInfo {
    std::string family;
    int size = 12;
    bool bold = false;
    bool italic = false;
    Color color;
};

enum class BackgroundMode {
    Fixed,     // фиксированный цвет + прозрачность
    Auto,      // автоматический (средний цвет из кадра)
    Gradient,  // градиентный авто-фон
    Inpaint    // "умный ластик" (inpainting)
};

enum class ModuleState {
    Idle,
    Running,
    Error,
    Recovering,
    Stopped
};

enum class ModuleType {
    Capture,
    OCR,
    Translator,
    Overlay
};

enum class CaptureMode {
    Callback,
    Timer,
    Hybrid
};

enum class PixelFormat {
    RGBA,
    NV12,
    BGRA
};

// ============================================================================
// Структуры данных
// ============================================================================

struct Region {
    Rect rect;
    bool ignored = false;  // true = игнорировать при захвате
};

struct Frame {
    std::vector<uint8_t> buffer;  // данные изображения
    uint64_t timestamp = 0;       // метка времени
    std::vector<Region> regions;  // области захвата/игнорирования
    Rect sourceRect;              // область захвата
    PixelFormat format = PixelFormat::RGBA;
    int width = 0;
    int height = 0;
    
    bool empty() const { return buffer.empty(); }
    size_t size() const { return buffer.size(); }
};

struct TextBlock {
    int id = 0;
    std::string text;
    Rect bbox;
    Color background;
    FontInfo font;
    float confidence = 0.0f;
};

struct Style {
    Color color = {255, 255, 255, 255};
    std::string fontFamily = "Arial";
    int fontSize = 14;
    bool bold = false;
    bool italic = false;
    float outline = 0.0f;
    BackgroundMode bgMode = BackgroundMode::Fixed;
    Color bgColor = {0, 0, 0, 180};
    float bgAlpha = 180.0f;
};

struct TranslatedBlock {
    int id = 0;
    std::string originalText;
    std::string translatedText;
    Rect bbox;
    Style style;
    float confidence = 0.0f;
};

// ============================================================================
// Интерфейсы модулей
// ============================================================================

class IModule {
public:
    virtual ~IModule() = default;
    virtual bool init() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual ModuleState getState() const = 0;
    virtual std::string getLastError() const = 0;
};

struct CaptureSource {
    enum Type { FullScreen, Window, Region } type = FullScreen;
    void* hwnd = nullptr;      // для Window (HWND на Windows)
    Rect regionRect;           // для Region
};

class ICapture : public IModule {
public:
    virtual bool setCaptureSource(const CaptureSource& source) = 0;
    virtual bool setIgnoreRegions(const std::vector<Rect>& regions) = 0;
    virtual void setCaptureMode(CaptureMode mode) = 0;
    virtual Frame getLatestFrame() = 0;
    virtual void setOnFrameCallback(std::function<void(const Frame&)> callback) = 0;
    virtual void setDebugMode(bool enable, const std::string& path = "/tmp/translator") = 0;
};

class IOCR : public IModule {
public:
    virtual void setLanguages(const std::vector<std::string>& langs) = 0;
    virtual void setPreprocessingFilters(const std::vector<std::string>& filters) = 0;
    virtual std::vector<TextBlock> processFrame(const Frame& frame) = 0;
    virtual void enableTextTracking(bool enable) = 0;
    virtual void setDebugMode(bool enable, const std::string& path = "/tmp/translator") = 0;
};

struct InferenceParams {
    float temperature = 0.7f;
    int topP = 40;
    int topK = 40;
    int maxTokens = 256;
};

class ITranslator : public IModule {
public:
    virtual void setSourceLanguage(const std::string& lang) = 0;
    virtual void setTargetLanguage(const std::string& lang) = 0;
    virtual void setInferenceParams(const InferenceParams& params) = 0;
    virtual TranslatedBlock translate(const TextBlock& block) = 0;
    virtual void setCacheSize(size_t size) = 0;
};

struct Hotkey {
    int id = 0;
    std::vector<int> keys;
    std::function<void()> callback;
    std::string description;
};

class IOverlay : public IModule {
public:
    virtual void setBackgroundMode(BackgroundMode mode) = 0;
    virtual void setTextStyle(const Style& style) = 0;
    virtual void setAutoFontMatch(bool enable) = 0;
    virtual void render(const std::vector<TranslatedBlock>& blocks, const Frame& originalFrame) = 0;
    virtual void setHotkeys(const std::vector<Hotkey>& keys) = 0;
    virtual void setVisible(bool visible) = 0;
    virtual void setOutputWindow(bool useSeparateWindow) = 0;
};

// ============================================================================
// Вспомогательные функции
// ============================================================================

inline std::string moduleTypeToString(ModuleType type) {
    switch (type) {
        case ModuleType::Capture: return "Capture";
        case ModuleType::OCR: return "OCR";
        case ModuleType::Translator: return "Translator";
        case ModuleType::Overlay: return "Overlay";
        default: return "Unknown";
    }
}

inline ModuleState stringToModuleState(const std::string& str) {
    if (str == "Idle") return ModuleState::Idle;
    if (str == "Running") return ModuleState::Running;
    if (str == "Error") return ModuleState::Error;
    if (str == "Recovering") return ModuleState::Recovering;
    return ModuleState::Stopped;
}

} // namespace translator

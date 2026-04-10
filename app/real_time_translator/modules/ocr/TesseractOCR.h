#pragma once

#include "../../core/Interfaces.h"

#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <sstream>

namespace translator {

/**
 * @brief OCR через Tesseract
 * 
 * Требует установки:
 *   pacman -S tesseract tesseract-data-eng tesseract-data-rus
 */
class TesseractOCR : public IOCR {
private:
    std::unique_ptr<tesseract::TessBaseAPI> api_;
    
    ModuleState state_ = ModuleState::Idle;
    std::string lastError_;
    
    // Языки для распознавания
    std::vector<std::string> languages_ = {"eng", "rus"};
    
    // Режим OCR
    tesseract::PageSegMode psms_ = tesseract::PSM_AUTO;
    
    // Для многопоточности
    std::mutex apiMutex_;
    
    // Text tracking
    bool textTrackingEnabled_ = false;

    // Для отладки
    bool saveDebugImages_ = false;
    std::string debugPath_ = "/tmp/translator";
    int debugImageCounter_ = 0;
    std::mutex debugMutex_;

    void saveDebugImage(const uint8_t* data, int width, int height, const std::string& suffix);

public:
    TesseractOCR();
    ~TesseractOCR() override;
    
    bool init() override;
    void start() override;
    void stop() override;
    
    ModuleState getState() const override { return state_; }
    std::string getLastError() const override { return lastError_; }
    
    // IOCR интерфейс
    void setLanguages(const std::vector<std::string>& langs) override;
    void setPreprocessingFilters(const std::vector<std::string>& filters) override;
    std::vector<TextBlock> processFrame(const Frame& frame) override;
    void enableTextTracking(bool enable) override;
    void setDebugMode(bool enable, const std::string& path = "/tmp/translator") override;

private:
    bool setupTesseract();
    std::string langCodes() const;
};

// ============================================================================
// Реализация
// ============================================================================

inline TesseractOCR::TesseractOCR() {
    api_ = std::make_unique<tesseract::TessBaseAPI>();
}

inline TesseractOCR::~TesseractOCR() {
    stop();
    if (api_) {
        api_->End();
    }
}

inline bool TesseractOCR::init() {
    try {
        state_ = ModuleState::Idle;
        lastError_.clear();
        
        return setupTesseract();
        
    } catch (const std::exception& e) {
        lastError_ = e.what();
        state_ = ModuleState::Error;
        return false;
    }
}

inline bool TesseractOCR::setupTesseract() {
    std::string langStr = langCodes();
    
    // Инициализация Tesseract
    if (api_->Init(nullptr, langStr.c_str(), tesseract::OEM_LSTM_ONLY)) {
        lastError_ = "Failed to initialize Tesseract (lang: " + langStr + ")";
        state_ = ModuleState::Error;
        return false;
    }
    
    // Установка режима сегментации
    api_->SetPageSegMode(psms_);
    
    // Настройка переменных для ускорения
    api_->SetVariable("textord_fast_passes", "1");
    
    state_ = ModuleState::Idle;
    return true;
}

inline std::string TesseractOCR::langCodes() const {
    std::string result;
    for (size_t i = 0; i < languages_.size(); i++) {
        if (i > 0) result += "+";
        result += languages_[i];
    }
    return result.empty() ? "eng" : result;
}

inline void TesseractOCR::start() {
    if (state_ == ModuleState::Running) return;
    
    if (state_ == ModuleState::Error) {
        if (!init()) return;
    }
    
    state_ = ModuleState::Running;
}

inline void TesseractOCR::stop() {
    state_ = ModuleState::Stopped;
}

inline void TesseractOCR::setLanguages(const std::vector<std::string>& langs) {
    languages_ = langs;
    // Переинициализируем если уже был инициализирован
    if (api_ && state_ != ModuleState::Idle) {
        api_->End();
        setupTesseract();
    }
}

inline void TesseractOCR::setPreprocessingFilters(const std::vector<std::string>& filters) {
    (void)filters;
    // Пока не реализовано - можно добавить предобработку изображения
}

inline void TesseractOCR::enableTextTracking(bool enable) {
    textTrackingEnabled_ = enable;
}

inline std::vector<TextBlock> TesseractOCR::processFrame(const Frame& frame) {
    if (state_ != ModuleState::Running) {
        return {};
    }
    
    if (frame.empty() || frame.buffer.empty()) {
        return {};
    }
    
    std::lock_guard lock(apiMutex_);
    
    try {
        int width = frame.width;
        int height = frame.height;
        
        // Создаём Pix из BGRA данных
        // Leptonica использует 32-битный формат с порядком RGB
        Pix* pix = pixCreate(width, height, 32);
        
        l_uint32* data = pixGetData(pix);
        int wpl = pixGetWpl(pix);
        
        // Конвертируем BGRA -> RGB в формат Leptonica
        const uint8_t* src = frame.buffer.data();
        
        for (int y = 0; y < height; y++) {
            l_uint32* line = data + y * wpl;
            for (int x = 0; x < width; x++) {
                int idx = (y * width + x) * 4;
                // Leptonica: (R << 24) | (G << 16) | (B << 8) | Alpha
                // Нам нужно: (B << 24) | (G << 16) | (R << 8) | 255
                l_uint32 pixel = 
                    (static_cast<l_uint32>(src[idx + 0]) << 24) |  // B
                    (static_cast<l_uint32>(src[idx + 1]) << 16) |  // G
                    (static_cast<l_uint32>(src[idx + 2]) << 8)  |  // R
                    0xFF;                                           // A
                line[x] = pixel;
            }
        }
        
        // Устанавливаем изображение
        api_->SetImage(pix);
        
        // Сохраняем изображение для отладки (уже преобразовано в pix)
        if (saveDebugImages_) {
            int counter;
            {
                std::lock_guard lock(debugMutex_);
                counter = debugImageCounter_++;
            }
            std::ostringstream oss;
            oss << debugPath_ << "_ocr_" << counter << ".png";
            pixWrite(oss.str().c_str(), pix, IFF_PNG);
        }

        // Получаем распознанный текст
        std::vector<TextBlock> blocks;
        char* outText = api_->GetUTF8Text();
        if (outText) {
            std::string text(outText);
            delete[] outText;
        
            // Выводим распознанный текст для отладки
            std::cout << "[TesseractOCR] RAW text: " << text.substr(0, 200) << "..." << std::endl;
            
            // Разбиваем текст на блоки по строкам
            std::istringstream iss(text);
            std::string line;
            int blockIndex = 0;
            
            while (std::getline(iss, line)) {
                if (line.empty()) continue;
                
                TextBlock block;
                block.id = blockIndex++;
                block.text = line;
                block.confidence = 0.9f;
                
                blocks.push_back(block);
                
                std::cout << "[TesseractOCR] Block " << block.id << ": " << line << std::endl;
            }
            
            if (!blocks.empty()) {
                std::cout << "[TesseractOCR] Total blocks: " << blocks.size() << std::endl;
            }
        } else {
            std::cout << "[TesseractOCR] No text recognized" << std::endl;
        }
        
        // Парсим результат
        tesseract::ResultIterator* ri = api_->GetIterator();
        if (ri) {
            int blockId = 0;
            for (ri->Begin(); ri->Next(tesseract::RIL_BLOCK); ) {
                const char* text = ri->GetUTF8Text(tesseract::RIL_BLOCK);
                if (!text) continue;
                
                std::string blockText = text;
                delete[] text;
                
                // Пропускаем пустые блоки
                if (blockText.find_first_not_of(" \n\t") == std::string::npos) {
                    continue;
                }
                
                // Получаем координаты
                int x1, y1, x2, y2;
                if (ri->BoundingBox(tesseract::RIL_BLOCK, &x1, &y1, &x2, &y2)) {
                    TextBlock block;
                    block.id = ++blockId;
                    block.text = blockText;
                    block.bbox = {
                        x1, y1,
                        x2 - x1,
                        y2 - y1
                    };
                    block.confidence = ri->Confidence(tesseract::RIL_BLOCK) / 100.0f;
                    blocks.push_back(block);
                }
            }
            delete ri;
        }
        
        // Если не получили блоки с координатами - создаём общий блок
        if (blocks.empty() && strlen(outText) > 0) {
            TextBlock block;
            block.id = 1;
            block.text = outText;
            block.bbox = {0, 0, width, height};
            block.confidence = 0.8f;
            blocks.push_back(block);
        }
        
        delete[] outText;
        pixDestroy(&pix);
        
        return blocks;
        
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return {};
    }
}

// ============================================================================
// Отладочные функции
// ============================================================================

inline void TesseractOCR::setDebugMode(bool enable, const std::string& path) {
    saveDebugImages_ = enable;
    debugPath_ = path;
}

inline void TesseractOCR::saveDebugImage(const uint8_t* rgbaData, int width, int height, const std::string& suffix) {
    if (!rgbaData || width <= 0 || height <= 0) return;
    
    int counter;
    {
        std::lock_guard lock(debugMutex_);
        counter = debugImageCounter_++;
    }
    
    // Сохраняем как PNG через Leptonica
    // Конвертируем BGRA в RGB
    std::vector<uint8_t> rgbBuf(width * height * 3);
    for (int i = 0; i < width * height; i++) {
        rgbBuf[i * 3 + 0] = rgbaData[i * 4 + 0];  // R
        rgbBuf[i * 3 + 1] = rgbaData[i * 4 + 1];  // G
        rgbBuf[i * 3 + 2] = rgbaData[i * 4 + 2];  // B
    }
    
    // Создаём Pix
    Pix* pix = pixCreate(width, height, 24);
    l_uint32* pixData = pixGetData(pix);
    int wpl = pixGetWpl(pix);
    
    for (int y = 0; y < height; y++) {
        l_uint32* dstLine = pixData + y * wpl;
        for (int x = 0; x < width; x++) {
            l_uint32 pixel = 
                (static_cast<l_uint32>(rgbBuf[(y * width + x) * 3 + 0]) << 16) |
                (static_cast<l_uint32>(rgbBuf[(y * width + x) * 3 + 1]) << 8) |
                static_cast<l_uint32>(rgbBuf[(y * width + x) * 3 + 2]);
            dstLine[x] = pixel;
        }
    }
    
    // Сохраняем PNG
    std::ostringstream oss;
    oss << debugPath_ << "_" << suffix << "_" << counter << ".png";
    std::string filepath = oss.str();
    
    pixWrite(filepath.c_str(), pix, IFF_PNG);
    pixDestroy(&pix);
}

} // namespace translator

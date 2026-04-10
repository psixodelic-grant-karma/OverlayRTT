#include "core/CoreEngine.h"
#include "modules/capture/GrimCapture.h"
#include "modules/ocr/TesseractOCR.h"
#include "modules/translator/BergamotTranslator.h"
#include "modules/overlay/StubOverlay.h"

#include <iostream>
#include <csignal>
#include <atomic>

// Глобальный указатель на движок для обработки сигналов
std::atomic<translator::CoreEngine*> g_engine{nullptr};

void signalHandler(int signal) {
    std::cout << "\n[Main] Received signal " << signal << ", shutting down..." << std::endl;
    
    auto* engine = g_engine.load();
    if (engine) {
        engine->stop();
    }
    
    exit(0);
}

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --help     Show this help" << std::endl;
    std::cout << "  --config   Path to config file (default: config/default_config.json)" << std::endl;
    std::cout << "  --verbose  Enable verbose output" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "============================================" << std::endl;
    std::cout << "  Real-Time Screen Translator v1.0.0" << std::endl;
    std::cout << "============================================" << std::endl;
    
    // Обработка аргументов командной строки
    bool verbose = false;
    std::string configPath;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }
    
    // Обработка сигналов
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    try {
        // Создаём движок
        translator::CoreEngine engine;
        g_engine.store(&engine);
        
        // Создаём модули
        auto capture = std::make_unique<translator::GrimCapture>();
        auto ocr = std::make_unique<translator::TesseractOCR>();
        auto translator = std::make_unique<translator::BergamotTranslator>();
        auto overlay = std::make_unique<translator::StubOverlay>();
        
        // Устанавливаем модули
        engine.setCapture(std::move(capture));
        engine.setOCR(std::move(ocr));
        engine.setTranslator(std::move(translator));
        engine.setOverlay(std::move(overlay));
        
        // Конфигурация
        translator::AppConfig config;
        config.capture.backend = "grim";
        config.capture.mode = translator::CaptureMode::Timer;
        config.capture.timerIntervalMs = 2000;  // 2 секунды (0.5 FPS)
        
        config.ocr.engine = "tesseract";
        config.ocr.languages = {"eng", "rus"};
        config.ocr.textTracking = true;
        
        config.translator.sourceLanguage = "auto";
        config.translator.targetLanguage = "ru";
        config.translator.cacheSize = 1000;
        
        config.overlay.bgMode = translator::BackgroundMode::Fixed;
        
        // Установить ДО инициализации модулей!
        config.general.logLevel = verbose ? translator::LogLevel::Debug : translator::LogLevel::Info;
        
        // Инициализация
        std::cout << "[Main] Initializing engine..." << std::endl;
        if (!engine.init(config)) {
            std::cerr << "[Main] Failed to initialize engine" << std::endl;
            return 1;
        }
        
        // Запуск
        std::cout << "[Main] Starting translation pipeline..." << std::endl;
        engine.start();
        
        std::cout << "[Main] Pipeline is running. Press Ctrl+C to stop." << std::endl;
        std::cout << "[Main] " << std::endl;
        
        // Главный цикл (ожидание)
        while (engine.isRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            // Вывод статистики
            auto ocrState = engine.getModuleState(translator::ModuleType::OCR);
            auto transState = engine.getModuleState(translator::ModuleType::Translator);
            
            if (verbose) {
                std::cout << "[Main] States - OCR: " << static_cast<int>(ocrState) 
                          << ", Translator: " << static_cast<int>(transState) << std::endl;
            }
        }
        
        // Остановка
        std::cout << "[Main] Stopping engine..." << std::endl;
        engine.stop();
        
        std::cout << "[Main] Done." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[Main] Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

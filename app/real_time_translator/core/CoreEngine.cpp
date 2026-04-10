#include "CoreEngine.h"
#include <iostream>
#include <sstream>
#include <algorithm>

namespace translator {

CoreEngine::CoreEngine() 
    : threadPool_(std::thread::hardware_concurrency())
{
    // Инициализация состояний модулей
    moduleStates_[ModuleType::Capture] = ModuleState::Idle;
    moduleStates_[ModuleType::OCR] = ModuleState::Idle;
    moduleStates_[ModuleType::Translator] = ModuleState::Idle;
    moduleStates_[ModuleType::Overlay] = ModuleState::Idle;
}

CoreEngine::~CoreEngine() {
    stop();
}

bool CoreEngine::init(const AppConfig& config) {
    std::cout << "[CoreEngine] Initializing..." << std::endl;
    
    // Создаём логгер
    logger_ = std::make_unique<Logger>();
    logger_->setLevel(config.general.logLevel);
    if (!config.general.logFile.empty()) {
        logger_->setOutputFile(config.general.logFile);
    }
    
    logger_->info("CoreEngine", "Initializing translation engine");
    
    // Создаём конфиг
    config_ = std::make_unique<ConfigManager>();
    
    // Инициализируем модули
    if (capture_) {
        capture_->setCaptureMode(config.capture.mode);
        
        // Включаем отладку если уровень логирования Debug
        if (config.general.logLevel == LogLevel::Debug) {
            capture_->setDebugMode(true, "/tmp/translator_capture");
        }
        
        if (!capture_->init()) {
            handleModuleError(ModuleType::Capture, "Failed to initialize capture module");
            return false;
        }
        setModuleState(ModuleType::Capture, ModuleState::Idle);
    }
    
    if (ocr_) {
        ocr_->setLanguages(config.ocr.languages);
        ocr_->enableTextTracking(config.ocr.textTracking);
        
        // Включаем отладку если уровень логирования Debug
        if (config.general.logLevel == LogLevel::Debug) {
            ocr_->setDebugMode(true, "/tmp/translator");
        }
        
        if (!ocr_->init()) {
            handleModuleError(ModuleType::OCR, "Failed to initialize OCR module");
            return false;
        }
        setModuleState(ModuleType::OCR, ModuleState::Idle);
    }
    
    if (translator_) {
        translator_->setSourceLanguage(config.translator.sourceLanguage);
        translator_->setTargetLanguage(config.translator.targetLanguage);
        translator_->setInferenceParams(config.translator.params);
        translator_->setCacheSize(config.translator.cacheSize);
        
        if (!translator_->init()) {
            handleModuleError(ModuleType::Translator, "Failed to initialize translator module");
            return false;
        }
        setModuleState(ModuleType::Translator, ModuleState::Idle);
    }
    
    if (overlay_) {
        overlay_->setBackgroundMode(config.overlay.bgMode);
        overlay_->setTextStyle(config.overlay.textStyle);
        overlay_->setAutoFontMatch(config.overlay.autoFontMatch);
        overlay_->setOutputWindow(config.overlay.separateWindow);
        
        if (!overlay_->init()) {
            handleModuleError(ModuleType::Overlay, "Failed to initialize overlay module");
            return false;
        }
        setModuleState(ModuleType::Overlay, ModuleState::Idle);
    }
    
    logger_->info("CoreEngine", "Initialization complete");
    return true;
}

void CoreEngine::start() {
    if (running_.load()) {
        std::cout << "[CoreEngine] Already running" << std::endl;
        return;
    }
    
    std::cout << "[CoreEngine] Starting..." << std::endl;
    logger_->info("CoreEngine", "Starting translation engine");
    
    running_.store(true);
    
    // Запускаем модули
    if (capture_) {
        capture_->start();
        setModuleState(ModuleType::Capture, ModuleState::Running);
    }
    
    if (ocr_) {
        ocr_->start();
        setModuleState(ModuleType::OCR, ModuleState::Running);
    }
    
    if (translator_) {
        translator_->start();
        setModuleState(ModuleType::Translator, ModuleState::Running);
    }
    
    if (overlay_) {
        overlay_->start();
        setModuleState(ModuleType::Overlay, ModuleState::Running);
    }
    
    // Запускаем рабочие потоки
    if (capture_) {
        workerThreads_.emplace_back(&CoreEngine::captureLoop, this);
    }
    
    if (ocr_) {
        workerThreads_.emplace_back(&CoreEngine::ocrLoop, this);
    }
    
    if (translator_) {
        workerThreads_.emplace_back(&CoreEngine::translateLoop, this);
    }
    
    if (overlay_) {
        workerThreads_.emplace_back(&CoreEngine::renderLoop, this);
    }
    
    logger_->info("CoreEngine", "Engine started successfully");
    std::cout << "[CoreEngine] Started with " << workerThreads_.size() << " worker threads" << std::endl;
}

void CoreEngine::stop() {
    if (!running_.load()) {
        return;
    }
    
    std::cout << "[CoreEngine] Stopping..." << std::endl;
    logger_->info("CoreEngine", "Stopping translation engine");
    
    running_.store(false);
    
    // Останавливаем модули
    if (capture_) {
        capture_->stop();
        setModuleState(ModuleType::Capture, ModuleState::Stopped);
    }
    
    if (ocr_) {
        ocr_->stop();
        setModuleState(ModuleType::OCR, ModuleState::Stopped);
    }
    
    if (translator_) {
        translator_->stop();
        setModuleState(ModuleType::Translator, ModuleState::Stopped);
    }
    
    if (overlay_) {
        overlay_->stop();
        setModuleState(ModuleType::Overlay, ModuleState::Stopped);
    }
    
    // Ждём завершения потоков
    for (auto& thread : workerThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    workerThreads_.clear();
    
    logger_->info("CoreEngine", "Engine stopped");
    std::cout << "[CoreEngine] Stopped" << std::endl;
}

void CoreEngine::setCapture(std::unique_ptr<ICapture> capture) {
    capture_ = std::move(capture);
}

void CoreEngine::setOCR(std::unique_ptr<IOCR> ocr) {
    ocr_ = std::move(ocr);
}

void CoreEngine::setTranslator(std::unique_ptr<ITranslator> translator) {
    translator_ = std::move(translator);
}

void CoreEngine::setOverlay(std::unique_ptr<IOverlay> overlay) {
    overlay_ = std::move(overlay);
}

ModuleState CoreEngine::getModuleState(ModuleType type) const {
    auto it = moduleStates_.find(type);
    if (it != moduleStates_.end()) {
        return it->second;
    }
    return ModuleState::Stopped;
}

std::string CoreEngine::getLastError(ModuleType type) const {
    auto it = lastErrors_.find(type);
    if (it != lastErrors_.end()) {
        return it->second;
    }
    return "";
}

void CoreEngine::updateConfig(const AppConfig& config) {
    if (ocr_) {
        ocr_->setLanguages(config.ocr.languages);
        ocr_->enableTextTracking(config.ocr.textTracking);
    }
    
    if (translator_) {
        translator_->setSourceLanguage(config.translator.sourceLanguage);
        translator_->setTargetLanguage(config.translator.targetLanguage);
        translator_->setCacheSize(config.translator.cacheSize);
    }
    
    if (overlay_) {
        overlay_->setBackgroundMode(config.overlay.bgMode);
        overlay_->setTextStyle(config.overlay.textStyle);
    }
}

// ============================================================================
// Циклы обработки (Throttle + Pending Frame)
// ============================================================================

void CoreEngine::captureLoop() {
    std::cout << "[CoreEngine] Capture loop started" << std::endl;
    logger_->debug("CoreEngine", "Capture loop started");
    
    while (running_) {
        try {
            if (!capture_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            // Получаем кадр
            Frame frame = capture_->getLatestFrame();
            
            if (frame.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            // Дедупликация - пропускаем идентичные кадры
            if (frame.timestamp == lastFrameTimestamp_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            lastFrameTimestamp_ = frame.timestamp;
            
            // Отправляем в очередь (перезаписывает предыдущий)
            context_.frameQueue.push(frame);
            context_.captureTimestamp.store(frame.timestamp);
            
        } catch (const std::exception& e) {
            handleModuleError(ModuleType::Capture, e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    std::cout << "[CoreEngine] Capture loop stopped" << std::endl;
}

void CoreEngine::ocrLoop() {
    std::cout << "[CoreEngine] OCR loop started" << std::endl;
    logger_->debug("CoreEngine", "OCR loop started");
    
    Frame currentFrame;
    Frame pendingFrame;
    
    while (running_) {
        try {
            // Пытаемся получить pending кадр (без блокировки)
            bool hasPending = context_.frameQueue.pop(pendingFrame);
            
            // Если модуль занят и есть pending - сохраняем
            if (ocrBusy_.load()) {
                if (hasPending) {
                    currentFrame = pendingFrame;  // перезаписываем
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            
            // Если нет данных - ждём
            if (!hasPending && !context_.frameQueue.hasData()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            // Получаем кадр для обработки
            if (hasPending) {
                currentFrame = pendingFrame;
            } else if (!context_.frameQueue.pop(currentFrame)) {
                continue;
            }
            
            if (!ocr_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            // Запускаем OCR (без отмены!)
            ocrBusy_.store(true);
            
            // Таймаут зависания
            auto taskStart = std::chrono::steady_clock::now();
            
            auto blocks = ocr_->processFrame(currentFrame);
            
            auto taskDuration = std::chrono::steady_clock::now() - taskStart;
            ocrBusy_.store(false);
            
            // Таймаут > 5 сек - ошибка
            if (taskDuration > std::chrono::seconds(5)) {
                handleModuleError(ModuleType::OCR, "Task timeout exceeded (5s)");
                continue;
            }
            
            // Дедупликация OCR результатов
            std::string currentHash = computeTextHash(blocks);
            if (currentHash == lastOCRResultHash_ && !blocks.empty()) {
                // Результат не изменился - пропускаем
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            lastOCRResultHash_ = currentHash;
            
            // Отправляем результат
            context_.textQueue.push(blocks);
            context_.ocrTimestamp.store(currentFrame.timestamp);
            
            logger_->debug("CoreEngine", "OCR processed " + std::to_string(blocks.size()) + " blocks");
            
        } catch (const std::exception& e) {
            ocrBusy_.store(false);
            handleModuleError(ModuleType::OCR, e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    std::cout << "[CoreEngine] OCR loop stopped" << std::endl;
}

void CoreEngine::translateLoop() {
    std::cout << "[CoreEngine] Translate loop started" << std::endl;
    logger_->debug("CoreEngine", "Translate loop started");
    
    std::vector<TextBlock> currentBlocks;
    std::vector<TextBlock> pendingBlocks;
    
    while (running_) {
        try {
            // Пытаемся получить pending блоки
            bool hasPending = context_.textQueue.pop(pendingBlocks);
            
            if (translatorBusy_.load()) {
                if (hasPending) {
                    currentBlocks = pendingBlocks;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            
            if (!hasPending && !context_.textQueue.hasData()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            if (hasPending) {
                currentBlocks = pendingBlocks;
            } else if (!context_.textQueue.pop(currentBlocks)) {
                continue;
            }
            
            if (!translator_ || currentBlocks.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            // Параллельная обработка блоков через ThreadPool
            translatorBusy_.store(true);
            
            std::vector<TranslatedBlock> results;
            results.reserve(currentBlocks.size());
            
            // Ставим задачи в пул
            std::vector<std::future<TranslatedBlock>> futures;
            for (const auto& block : currentBlocks) {
                futures.push_back(threadPool_.enqueue([this, &block]() {
                    return translator_->translate(block);
                }));
            }
            
            // Собираем результаты
            for (auto& future : futures) {
                try {
                    results.push_back(future.get());
                } catch (const std::exception& e) {
                    logger_->error("Translator", std::string("Block translation failed: ") + e.what());
                }
            }
            
            translatorBusy_.store(false);
            
            // Отправляем результаты
            context_.translatedQueue.push(results);
            context_.translatorTimestamp.store(context_.ocrTimestamp.load());
            
            logger_->debug("CoreEngine", "Translated " + std::to_string(results.size()) + " blocks");
            
        } catch (const std::exception& e) {
            translatorBusy_.store(false);
            handleModuleError(ModuleType::Translator, e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    std::cout << "[CoreEngine] Translate loop stopped" << std::endl;
}

void CoreEngine::renderLoop() {
    std::cout << "[CoreEngine] Render loop started" << std::endl;
    logger_->debug("CoreEngine", "Render loop started");
    
    std::vector<TranslatedBlock> currentBlocks;
    
    while (running_) {
        try {
            // Ждём данные с таймаутом
            if (!context_.translatedQueue.waitPop(currentBlocks, 
                std::chrono::milliseconds(50))) {
                continue;
            }
            
            if (!overlay_ || currentBlocks.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            // Получаем оригинальный кадр для авто-фона
            Frame originalFrame;
            context_.frameQueue.pop(originalFrame);
            
            // Рендерим
            overlay_->render(currentBlocks, originalFrame);
            context_.overlayTimestamp.store(context_.translatorTimestamp.load());
            
        } catch (const std::exception& e) {
            handleModuleError(ModuleType::Overlay, e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    std::cout << "[CoreEngine] Render loop stopped" << std::endl;
}

// ============================================================================
// Обработка ошибок
// ============================================================================

void CoreEngine::handleModuleError(ModuleType type, const std::string& error) {
    std::string msg = "Module " + moduleTypeToString(type) + " error: " + error;
    std::cout << "[CoreEngine] " << msg << std::endl;
    
    if (logger_) {
        logger_->error("CoreEngine", msg);
    }
    
    // Сохраняем ошибку
    lastErrors_[type] = error;
    setModuleState(type, ModuleState::Error);
    
    // Пытаемся восстановить
    if (isRecoverable(type)) {
        setModuleState(type, ModuleState::Recovering);
        recoverModule(type);
    } else {
        logger_->critical("CoreEngine", 
            "Non-recoverable error in " + moduleTypeToString(type) + ". Stopping.");
        stop();
    }
}

bool CoreEngine::isRecoverable(ModuleType type) const {
    // Все модули кроме Capture считаются восстанавливаемыми
    // Capture может быть критичен если потеряно окно
    auto it = lastErrors_.find(type);
    if (type == ModuleType::Capture && it != lastErrors_.end()) {
        return it->second.find("window lost") == std::string::npos;
    }
    return true;
}

void CoreEngine::recoverModule(ModuleType type) {
    constexpr auto RECOVERY_TIMEOUT = std::chrono::seconds(5);
    auto startTime = std::chrono::steady_clock::now();
    
    std::cout << "[CoreEngine] Recovering module: " << moduleTypeToString(type) << std::endl;
    logger_->warning("CoreEngine", "Attempting to recover " + moduleTypeToString(type));
    
    while (running_) {
        try {
            IModule* module = nullptr;
            switch (type) {
                case ModuleType::Capture: module = capture_.get(); break;
                case ModuleType::OCR: module = ocr_.get(); break;
                case ModuleType::Translator: module = translator_.get(); break;
                case ModuleType::Overlay: module = overlay_.get(); break;
                default: break;
            }
            
            if (module) {
                module->stop();
                if (module->init()) {
                    module->start();
                    setModuleState(type, ModuleState::Running);
                    logger_->info("CoreEngine", "Successfully recovered " + moduleTypeToString(type));
                    std::cout << "[CoreEngine] Recovered: " << moduleTypeToString(type) << std::endl;
                    return;
                }
            }
        } catch (...) {
            // Игнорируем ошибки при восстановлении
        }
        
        // Проверяем таймаут
        if (std::chrono::steady_clock::now() - startTime > RECOVERY_TIMEOUT) {
            logger_->error("CoreEngine", "Recovery timeout for " + moduleTypeToString(type));
            setModuleState(type, ModuleState::Error);
            return;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void CoreEngine::setModuleState(ModuleType type, ModuleState state) {
    moduleStates_[type] = state;
}

void CoreEngine::createDefaultConfig() {
    // Заглушка - можно расширить
}

std::string CoreEngine::computeTextHash(const std::vector<TextBlock>& blocks) {
    if (blocks.empty()) return "";
    
    std::stringstream ss;
    for (const auto& block : blocks) {
        ss << block.id << ":" << block.text << ";";
    }
    return ss.str();
}

} // namespace translator

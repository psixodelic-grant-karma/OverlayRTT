#pragma once

#include "Interfaces.h"
#include "../utils/LatestQueue.h"
#include "../utils/ThreadPool.h"
#include "../utils/Logger.h"
#include "../utils/ConfigManager.h"

#include <memory>
#include <unordered_map>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

namespace translator {

/**
 * @brief Главный класс системы - ядро конвейера
 * 
 * Реализует механизм throttle + pending frame:
 * - Каждый модуль обрабатывает не более одной задачи
 * - Новые данные сохраняются как pending (только последние)
 * - Никаких отмен - задача доводится до конца
 */
class CoreEngine {
private:
    // Модули
    std::unique_ptr<ICapture> capture_;
    std::unique_ptr<IOCR> ocr_;
    std::unique_ptr<ITranslator> translator_;
    std::unique_ptr<IOverlay> overlay_;

    // Контекст конвейера (очереди данных)
    struct PipelineContext {
        LatestQueue<Frame> frameQueue;                              // Capture → OCR
        LatestQueue<std::vector<TextBlock>> textQueue;              // OCR → Translator
        LatestQueue<std::vector<TranslatedBlock>> translatedQueue;  // Translator → Overlay
        
        // Метрики времени
        std::atomic<uint64_t> captureTimestamp{0};
        std::atomic<uint64_t> ocrTimestamp{0};
        std::atomic<uint64_t> translatorTimestamp{0};
        std::atomic<uint64_t> overlayTimestamp{0};
    } context_;

    // Потоки
    std::vector<std::thread> workerThreads_;
    std::atomic<bool> running_{false};

    // Состояния модулей
    std::unordered_map<ModuleType, ModuleState> moduleStates_;
    std::unordered_map<ModuleType, std::string> lastErrors_;

    // Флаги занятости для throttle + pending frame
    std::atomic<bool> ocrBusy_{false};
    std::atomic<bool> translatorBusy_{false};

    // Вспомогательные компоненты
    std::unique_ptr<Logger> logger_;
    std::unique_ptr<ConfigManager> config_;
    ThreadPool threadPool_;

    // Последний обработанный кадр (для дедупликации)
    uint64_t lastFrameTimestamp_ = 0;
    std::string lastOCRResultHash_;

public:
    CoreEngine();
    ~CoreEngine();

    /**
     * @brief Инициализация движка
     * @param config конфигурация
     * @return true при успехе
     */
    bool init(const AppConfig& config);

    /**
     * @brief Запустить конвейер
     */
    void start();

    /**
     * @brief Остановить конвейер
     */
    void stop();

    /**
     * @brief Установить модуль захвата
     */
    void setCapture(std::unique_ptr<ICapture> capture);

    /**
     * @brief Установить модуль OCR
     */
    void setOCR(std::unique_ptr<IOCR> ocr);

    /**
     * @brief Установить модуль перевода
     */
    void setTranslator(std::unique_ptr<ITranslator> translator);

    /**
     * @brief Установить модуль оверлея
     */
    void setOverlay(std::unique_ptr<IOverlay> overlay);

    /**
     * @brief Получить состояние модуля
     */
    ModuleState getModuleState(ModuleType type) const;

    /**
     * @brief Получить последнюю ошибку модуля
     */
    std::string getLastError(ModuleType type) const;

    /**
     * @brief Обновить конфигурацию
     */
    void updateConfig(const AppConfig& config);

    /**
     * @brief Проверить работает ли движок
     */
    bool isRunning() const { return running_.load(); }

private:
    // Циклы обработки
    void captureLoop();
    void ocrLoop();
    void translateLoop();
    void renderLoop();

    // Обработка ошибок
    void handleModuleError(ModuleType type, const std::string& error);
    void recoverModule(ModuleType type);
    bool isRecoverable(ModuleType type) const;

    // Вспомогательные
    void createDefaultConfig();
    void setModuleState(ModuleType type, ModuleState state);
    std::string computeTextHash(const std::vector<TextBlock>& blocks);
};

} // namespace translator

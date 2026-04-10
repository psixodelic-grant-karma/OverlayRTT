# Архитектура системы экранного перевода в реальном времени

---

## 1. Обзор системы

Система состоит из 4 независимых асинхронных модулей:
1. **Capture** — захват экрана
2. **OCR** — распознавание текста
3. **Translator** — перевод
4. **Overlay** — отображение перевода

Ключевой принцип — **механизм throttle + pending frame**: каждый модуль обрабатывает не более одной задачи, сохраняя последние входящие данные как pending.

---

## 2. Основные структуры данных

```cpp
// Кадр с экрана
struct Frame {
    ImageBuffer buffer;          // RGBA/NV12 данные изображения
    uint64_t timestamp;          // метка времени
    std::vector<Region> regions; // области захвата/игнорирования
    Rect sourceRect;             // область захвата
    PixelFormat format;          // формат пикселей
};

// Область захвата
struct Region {
    Rect rect;                   // прямоугольник
    bool ignored;                // true = игнорировать при захвате
};

// Текстовый блок после OCR
struct TextBlock {
    int id;                      // уникальный ID блока
    std::string text;            // распознанный текст
    Rect bbox;                   // ограничивающий прямоугольник
    Color background;            // средний цвет фона
    FontInfo font;               // параметры шрифта
    float confidence;            // достоверность распознавания (0.0 - 1.0)
};

// Переведённый блок
struct TranslatedBlock {
    int id;                      // ID соответствующего TextBlock
    std::string originalText;    // оригинальный текст
    std::string translatedText;  // переведённый текст
    Rect bbox;                   // позиция на экране
    Style style;                 // стиль отображения
    float confidence;            // достоверность перевода
};

// Стиль текста оверлея
struct Style {
    Color color;                 // цвет текста
    std::string fontFamily;      // гарнитура шрифта
    int fontSize;                // размер шрифта
    bool bold;                   // жирный
    bool italic;                 // курсив
    float outline;               // ширина обводки
    BackgroundMode bgMode;       // режим фона
    Color bgColor;               // цвет фона (если фиксированный)
    float bgAlpha;               // прозрачность фона (0..255)
};

// Режимы фона подложки
enum class BackgroundMode {
    Fixed,       // фиксированный цвет + прозрачность
    Auto,        // автоматический (средний цвет из кадра)
    Gradient,    // градиентный авто-фон
    Inpaint      // "умный ластик" (inpainting)
};

// Состояние модуля (для обработки ошибок)
enum class ModuleState {
    Idle,
    Running,
    Error,
    Recovering,
    Stopped
};

// Тип модуля
enum class ModuleType {
    Capture,
    OCR,
    Translator,
    Overlay
};
```

---

## 3. Базовые интерфейсы

### 3.1 Базовый интерфейс модуля

```cpp
class IModule {
public:
    virtual ~IModule() = default;
    
    virtual bool init() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual ModuleState getState() const = 0;
    virtual std::string getLastError() const = 0;
};
```

### 3.2 Интерфейс захвата

```cpp
class ICapture : public IModule {
public:
    virtual bool setCaptureSource(const CaptureSource& source) = 0;
    virtual bool setIgnoreRegions(const std::vector<Rect>& regions) = 0;
    virtual void setCaptureMode(CaptureMode mode) = 0;
    virtual Frame getLatestFrame() = 0;
    virtual void setOnFrameCallback(std::function<void(const Frame&)> callback) = 0;
};

// Источник захвата
struct CaptureSource {
    enum Type { FullScreen, Window, Region } type;
    HWND hwnd;          // для Window
    Rect regionRect;    // для Region
};

// Режим захвата
enum class CaptureMode { Callback, Timer, Hybrid };
```

### 3.3 Интерфейс OCR

```cpp
class IOCR : public IModule {
public:
    virtual void setLanguages(const std::vector<std::string>& langs) = 0;
    virtual void setPreprocessingFilters(const std::vector<Filter>& filters) = 0;
    virtual std::vector<TextBlock> processFrame(const Frame& frame) = 0;
    virtual void enableTextTracking(bool enable) = 0;
};
```

### 3.4 Интерфейс переводчика

```cpp
class ITranslator : public IModule {
public:
    virtual void setSourceLanguage(const std::string& lang) = 0;
    virtual void setTargetLanguage(const std::string& lang) = 0;
    virtual void setInferenceParams(const InferenceParams& params) = 0;
    virtual TranslatedBlock translate(const TextBlock& block) = 0;
    virtual void setCacheSize(size_t size) = 0;
};
```

### 3.5 Интерфейс оверлея

```cpp
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
```

---

## 4. Очереди и обмен данными (Throttle + Pending Frame)

### 4.1 Single-Slot Queue (перезаписываемая очередь)

```cpp
template<typename T>
class LatestQueue {
private:
    std::atomic<bool> has_pending_{false};
    T pending_value_;
    std::mutex mtx_;
    std::condition_variable cv_;

public:
    // Перезаписывает предыдущее значение (всегда хранит только последнее)
    void push(const T& value) {
        std::lock_guard lock(mtx_);
        pending_value_ = value;
        has_pending_.store(true, std::memory_order_release);
        cv_.notify_one();
    }
    
    // Неблокирующее получение
    bool pop(T& out) {
        if (!has_pending_.load(std::memory_order_acquire))
            return false;
        
        std::lock_guard lock(mtx_);
        if (!has_pending_.load())
            return false;
        
        out = std::move(pending_value_);
        has_pending_.store(false);
        return true;
    }
    
    // Ожидание с таймаутом (для потоков)
    bool waitPop(T& out, std::chrono::milliseconds timeout) {
        std::unique_lock lock(mtx_);
        if (!cv_.wait_for(lock, timeout, [this] { 
            return has_pending_.load(std::memory_order_acquire); 
        })) {
            return false; // таймаут
        }
        
        out = std::move(pending_value_);
        has_pending_.store(false);
        return true;
    }
    
    bool hasData() const {
        return has_pending_.load(std::memory_order_acquire);
    }
};
```

### 4.2 Глобальный контекст конвейера

```cpp
class PipelineContext {
public:
    LatestQueue<Frame> frameQueue;                           // Capture → OCR
    LatestQueue<std::vector<TextBlock>> textQueue;           // OCR → Translator
    LatestQueue<std::vector<TranslatedBlock>> translatedQueue; // Translator → Overlay
    
    // Для отладки и профилирования
    std::atomic<uint64_t> captureTimestamp{0};
    std::atomic<uint64_t> ocrTimestamp{0};
    std::atomic<uint64_t> translatorTimestamp{0};
    std::atomic<uint64_t> overlayTimestamp{0};
};
```

---

## 5. Ядро системы (CoreEngine)

### 5.1 Главный класс

```cpp
class CoreEngine {
private:
    // Модули
    std::unique_ptr<ICapture> capture_;
    std::unique_ptr<IOCR> ocr_;
    std::unique_ptr<ITranslator> translator_;
    std::unique_ptr<IOverlay> overlay_;
    
    // Контекст конвейера
    PipelineContext context_;
    
    // Потоки
    std::vector<std::thread> workerThreads_;
    std::atomic<bool> running_{false};
    
    // Состояния модулей
    std::unordered_map<ModuleType, ModuleState> moduleStates_;
    std::unordered_map<ModuleType, std::string> lastErrors_;
    
    // Для throttle + pending frame
    std::atomic<bool> ocrBusy_{false};
    std::atomic<bool> translatorBusy_{false};
    
    // Вспомогательные компоненты
    std::unique_ptr<Logger> logger_;
    std::unique_ptr<Profiler> profiler_;
    std::unique_ptr<ConfigManager> config_;
    ThreadPool threadPool_;  // для параллельного перевода

public:
    CoreEngine();
    ~CoreEngine();
    
    // Инициализация и запуск
    bool init(const AppConfig& config);
    void start();
    void stop();
    
    // Управление модулями
    void setCapture(std::unique_ptr<ICapture> capture);
    void setOCR(std::unique_ptr<IOCR> ocr);
    void setTranslator(std::unique_ptr<ITranslator> translator);
    void setOverlay(std::unique_ptr<IOverlay> overlay);
    
    // Состояние
    ModuleState getModuleState(ModuleType type) const;
    std::string getLastError(ModuleType type) const;
    
    // Конфигурация
    void updateConfig(const AppConfig& config);
    
private:
    void captureLoop();
    void ocrLoop();
    void translateLoop();
    void renderLoop();
    
    void handleModuleError(ModuleType type, const std::string& error);
    void recoverModule(ModuleType type);
};
```

### 5.2 Реализация циклов с throttle + pending frame

#### Цикл захвата (Capture Loop)

```cpp
void CoreEngine::captureLoop() {
    while (running_) {
        try {
            // Получаем кадр (блокирующий или по таймеру)
            Frame frame = capture_->getLatestFrame();
            
            if (frame.buffer.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            // Проверяем, изменился ли кадр
            static uint64_t lastTimestamp = 0;
            if (frame.timestamp == lastTimestamp) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            lastTimestamp = frame.timestamp;
            
            // Отправляем в очередь (перезаписывает предыдущий)
            context_.frameQueue.push(frame);
            context_.captureTimestamp.store(frame.timestamp);
            
        } catch (const std::exception& e) {
            handleModuleError(ModuleType::Capture, e.what());
        }
    }
}
```

#### Цикл OCR с throttle + pending

```cpp
void CoreEngine::ocrLoop() {
    Frame currentFrame;
    Frame pendingFrame;
    
    while (running_) {
        try {
            // Пытаемся получить pending кадр (без ожидания)
            bool hasPending = context_.frameQueue.pop(pendingFrame);
            
            // Если модуль занят и есть pending - сохраняем
            if (ocrBusy_.load()) {
                if (hasPending) {
                    // Перезаписываем pending (храним только последний)
                    currentFrame = pendingFrame;
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
            
            // Запускаем OCR (без отмены!)
            ocrBusy_.store(true);
            
            // Проверка на таймаут зависания
            auto taskStart = std::chrono::steady_clock::now();
            auto blocks = ocr_->processFrame(currentFrame);
            auto taskDuration = std::chrono::steady_clock::now() - taskStart;
            
            ocrBusy_.store(false);
            
            // Таймаут > 5 сек - ошибка
            if (taskDuration > std::chrono::seconds(5)) {
                handleModuleError(ModuleType::OCR, "Task timeout exceeded");
                continue;
            }
            
            // Отправляем результат
            context_.textQueue.push(blocks);
            context_.ocrTimestamp.store(currentFrame.timestamp);
            
        } catch (const std::exception& e) {
            ocrBusy_.store(false);
            handleModuleError(ModuleType::OCR, e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}
```

#### Цикл переводчика с многопоточностью

```cpp
void CoreEngine::translateLoop() {
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
            
            // Параллельная обработка блоков через ThreadPool
            translatorBusy_.store(true);
            
            std::vector<TranslatedBlock> results;
            results.reserve(currentBlocks.size());
            
            // Используем ThreadPool для параллельного перевода
            std::vector<std::future<TranslatedBlock>> futures;
            
            for (const auto& block : currentBlocks) {
                // Кладём задачу в пул
                futures.push_back(threadPool_.enqueue([this, &block]() {
                    return translator_->translate(block);
                }));
            }
            
            // Собираем результаты
            for (auto& future : futures) {
                try {
                    results.push_back(future.get());
                } catch (const std::exception& e) {
                    // Логируем ошибку, но продолжаем
                    logger_->error("Block translation failed: {}", e.what());
                }
            }
            
            translatorBusy_.store(false);
            
            // Отправляем результаты
            context_.translatedQueue.push(results);
            context_.translatorTimestamp.store(
                context_.ocrTimestamp.load()
            );
            
        } catch (const std::exception& e) {
            translatorBusy_.store(false);
            handleModuleError(ModuleType::Translator, e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}
```

#### Цикл оверлея

```cpp
void CoreEngine::renderLoop() {
    std::vector<TranslatedBlock> currentBlocks;
    
    while (running_) {
        try {
            // Ждём данные с таймаутом
            if (!context_.translatedQueue.waitPop(currentBlocks, 
                std::chrono::milliseconds(50))) {
                continue;
            }
            
            // Получаем оригинальный кадр для авто-фона
            Frame originalFrame;
            context_.frameQueue.pop(originalFrame); // не блокируем
            
            // Рендерим
            overlay_->render(currentBlocks, originalFrame);
            context_.overlayTimestamp.store(
                context_.translatorTimestamp.load()
            );
            
        } catch (const std::exception& e) {
            handleModuleError(ModuleType::Overlay, e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}
```

---

## 6. Система плагинов

### 6.1 Базовый интерфейс плагина

```cpp
class IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual bool init(const Config& cfg) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual PluginType getType() const = 0;
    virtual std::string getName() const = 0;
    virtual std::string getVersion() const = 0;
};
```

### 6.2 Менеджер плагинов

```cpp
class PluginManager {
public:
    bool loadPluginsFromDir(const fs::path& dir);
    void registerPlugin(std::shared_ptr<IPlugin> plugin);
    
    template<typename T>
    std::shared_ptr<T> getPlugin(PluginType type);
    
    std::vector<std::shared_ptr<IPlugin>> getAllPlugins() const;
    void unloadAll();

private:
    std::unordered_map<PluginType, std::vector<std::shared_ptr<IPlugin>>> plugins_;
    std::unordered_map<std::string, std::shared_ptr<IPlugin>> pluginsByName_;
};
```

---

## 7. Кэширование переводов

### 7.1 LRU-кэш

```cpp
template<typename Key, typename Value>
class LRUCache {
private:
    size_t capacity_;
    std::list<std::pair<Key, Value>> items_;
    std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator> map_;

public:
    explicit LRUCache(size_t capacity) : capacity_(capacity) {}
    
    void put(const Key& key, const Value& value) {
        auto it = map_.find(key);
        
        if (it != map_.end()) {
            // Обновляем существующий
            it->second->second = value;
            items_.splice(items_.begin(), items_, it->second);
            return;
        }
        
        // Добавляем новый
        if (items_.size() >= capacity_) {
            // Удаляем самый старый
            auto last = items_.back();
            map_.erase(last.first);
            items_.pop_back();
        }
        
        items_.push_front({key, value});
        map_[key] = items_.begin();
    }
    
    std::optional<Value> get(const Key& key) {
        auto it = map_.find(key);
        if (it == map_.end())
            return std::nullopt;
        
        // Перемещаем в начало (most recently used)
        items_.splice(items_.begin(), items_, it->second);
        return it->second->second;
    }
    
    bool contains(const Key& key) const {
        return map_.find(key) != map_.end();
    }
    
    void clear() {
        items_.clear();
        map_.clear();
    }
};
```

### 7.2 Интеграция в переводчик

```cpp
class LLMTranslator : public ITranslator {
private:
    LRUCache<std::string, std::string> translationCache_;
    // ... остальные члены
    
public:
    TranslatedBlock translate(const TextBlock& block) override {
        // Проверяем кэш
        auto cached = translationCache_.get(block.text);
        if (cached.has_value()) {
            return createTranslatedBlock(block.id, block.text, *cached, block.bbox);
        }
        
        // Выполняем перевод
        std::string translated = performTranslation(block.text);
        
        // Кладём в кэш
        translationCache_.put(block.text, translated);
        
        return createTranslatedBlock(block.id, block.text, translated, block.bbox);
    }
    
    void setCacheSize(size_t size) override {
        // Создаём новый кэш нужного размера
        translationCache_ = LRUCache<std::string, std::string>(size);
    }
};
```

---

## 8. Обработка ошибок

### 8.1 Класс ошибки

```cpp
class ModuleException : public std::runtime_error {
private:
    ModuleType module_;
    bool recoverable_;

public:
    ModuleException(ModuleType module, const std::string& msg, bool recoverable = true)
        : std::runtime_error(msg), module_(module), recoverable_(recoverable) {}
    
    ModuleType getModule() const { return module_; }
    bool isRecoverable() const { return recoverable_; }
};
```

### 8.2 Расширенная обработка в CoreEngine

```cpp
void CoreEngine::handleModuleError(ModuleType type, const std::string& error) {
    // Логируем ошибку
    logger_->error("Module {} error: {}", 
        moduleTypeToString(type), error);
    
    // Сохраняем ошибку
    lastErrors_[type] = error;
    moduleStates_[type] = ModuleState::Error;
    
    // Пытаемся восстановить
    if (isRecoverable(type)) {
        moduleStates_[type] = ModuleState::Recovering;
        recoverModule(type);
    } else {
        // Критическая ошибка - останавливаем
        logger_->critical("Non-recoverable error in {}. Stopping.", 
            moduleTypeToString(type));
        stop();
    }
}

bool CoreEngine::isRecoverable(ModuleType type) const {
    // Все модули кроме Capture считаются восстанавливаемыми
    // Capture может быть критичен если потеряно окно
    return type != ModuleType::Capture || 
           lastErrors_[type].find("window lost") == std::string::npos;
}

void CoreEngine::recoverModule(ModuleType type) {
    constexpr auto RECOVERY_TIMEOUT = std::chrono::seconds(5);
    auto startTime = std::chrono::steady_clock::now();
    
    while (running_) {
        try {
            switch (type) {
                case ModuleType::Capture:
                    capture_->stop();
                    if (capture_->init() && capture_->start()) {
                        moduleStates_[type] = ModuleState::Running;
                        return;
                    }
                    break;
                    
                case ModuleType::OCR:
                    ocr_->stop();
                    if (ocr_->init() && ocr_->start()) {
                        moduleStates_[type] = ModuleState::Running;
                        return;
                    }
                    break;
                    
                case ModuleType::Translator:
                    translator_->stop();
                    if (translator_->init() && translator_->start()) {
                        moduleStates_[type] = ModuleState::Running;
                        return;
                    }
                    break;
                    
                case ModuleType::Overlay:
                    overlay_->stop();
                    if (overlay_->init() && overlay_->start()) {
                        moduleStates_[type] = ModuleState::Running;
                        return;
                    }
                    break;
            }
        } catch (...) {
            // Игнорируем ошибки при восстановлении
        }
        
        // Проверяем таймаут
        if (std::chrono::steady_clock::now() - startTime > RECOVERY_TIMEOUT) {
            logger_->error("Recovery timeout for {}", moduleTypeToString(type));
            moduleStates_[type] = ModuleState::Error;
            return;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
```

### 8.3 Таймаут зависания

```cpp
// Таймаут зависания встроен в ocrLoop() и translateLoop()
// см. раздел 5.2
// При превышении 5 секунд - сброс задачи и переход к pending
```

---

## 9. Вспомогательные компоненты

### 9.1 Логирование

```cpp
class Logger {
public:
    enum Level { Debug, Info, Warning, Error, Critical };
    
    void log(Level level, const std::string& module, const std::string& message);
    void debug(const std::string& module, const std::string& msg);
    void info(const std::string& module, const std::string& msg);
    void warning(const std::string& module, const std::string& msg);
    void error(const std::string& module, const std::string& msg);
    void critical(const std::string& module, const std::string& msg);
    
    void setLevel(Level level);
    void setOutputFile(const fs::path& path);
    void rotate(); // ротация логов
};
```

### 9.2 Профилирование

```cpp
class Profiler {
public:
    struct Metrics {
        uint64_t captureToOCR;   // мс
        uint64_t ocrToTranslate; // мс
        uint64_t translateToRender; // мс
        uint64_t totalLatency;   // мс
    };
    
    void recordCapture(Frame& frame);
    void recordOCR(const std::vector<TextBlock>& blocks);
    void recordTranslate(const std::vector<TranslatedBlock>& blocks);
    void recordRender();
    
    Metrics getMetrics() const;
    void reset();
    
    // Отправка метрик в GUI
    std::function<void(const Metrics&)> onMetricsUpdate;
};
```

### 9.3 Менеджер горячих клавиш

```cpp
class HotkeyManager {
public:
    struct Hotkey {
        int id;
        std::vector<int> keys; // виртуальные коды клавиш
        std::function<void()> callback;
        std::string description;
    };
    
    bool registerHotkey(const Hotkey& hotkey);
    void unregisterHotkey(int id);
    void unregisterAll();
    
    // Windows: RegisterHotKey
    // Linux: XGrabKey
};
```

### 9.4 ThreadPool

```cpp
class ThreadPool {
private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queueMutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;
    size_t maxThreads_;

public:
    explicit ThreadPool(size_t threads = std::thread::hardware_concurrency());
    ~ThreadPool();
    
    template<typename F>
    std::future<std::result_of_t<F()>> enqueue(F&& task);
    
    void waitAll();
    size_t getQueueSize() const;
    size_t getThreadCount() const;
};
```

### 9.5 Менеджер конфигурации

```cpp
class ConfigManager {
public:
    bool load(const fs::path& path);
    bool save(const fs::path& path) const;
    
    template<typename T>
    T get(const std::string& key) const;
    
    template<typename T>
    void set(const std::string& key, const T& value);
    
    void subscribe(std::function<void()> callback); // уведомление об изменениях

private:
    nlohmann::json config_;
    fs::path configPath_;
};
```

---

## 10. Реализации модулей

### 10.1 Capture

```cpp
class DXGICapture : public ICapture { /* Windows DXGI */ };
class OpenGLCapture : public ICapture { /* OpenGL + PBO */ };
class VulkanCapture : public ICapture { /* Vulkan */ };
class ScreenCaptureLiteCapture : public ICapture { /* screen_capture_lite */ };
class FFmpegCapture : public ICapture { /* FFmpeg fallback */ };
```

### 10.2 OCR

```cpp
class TesseractOCR : public IOCR { /* Tesseract API */ };
class PaddleOCR : public IOCR { /* PaddlePaddle */ };
```

### 10.3 Translator

```cpp
class LLMTranslator : public ITranslator { /* llama.cpp */ };
class MarianTranslator : public ITranslator { /* Marian NMT */ };
class ArgosTranslator : public ITranslator { /* Argos Translate */ };
```

### 10.4 Overlay

```cpp
class OpenGLOverlay : public IOverlay { /* OpenGL текстуры */ };
class VulkanOverlay : public IOverlay { /* Vulkan */ };
class WinAPIOverlay : public IOverlay { /* WS_EX_LAYERED */ };
```

---

## 11. Конфигурация приложения

```cpp
struct AppConfig {
    CaptureConfig capture;
    OCRConfig ocr;
    TranslatorConfig translator;
    OverlayConfig overlay;
    GeneralConfig general;
};

struct CaptureConfig {
    std::string backend;         // dxgi, opengl, vulkan, screen_capture_lite, ffmpeg
    CaptureSource source;
    CaptureMode mode;
    int timerIntervalMs;         // для Timer режима
    std::vector<Rect> ignoreRegions;
    PixelFormat format;
};

struct OCRConfig {
    std::string engine;          // tesseract, paddleocr
    std::vector<std::string> languages; // {"eng", "rus"}
    std::vector<Filter> preprocessing;
    bool textTracking;
    float minTextSize;
};

struct TranslatorConfig {
    std::string modelPath;       // путь к .gguf файлу
    std::string sourceLanguage;  // "auto" или код
    std::string targetLanguage;  // код (ru, en, zh...)
    size_t cacheSize;            // размер LRU кэша
    int threads;
    InferenceParams params;
};

struct OverlayConfig {
    BackgroundMode bgMode;
    Color bgColor;
    float bgAlpha;
    Style textStyle;
    bool autoFontMatch;
    bool separateWindow;
    std::vector<Hotkey> hotkeys;
};

struct GeneralConfig {
    bool startMinimized;
    bool autoStart;
    LogLevel logLevel;
    fs::path logFile;
};
```

---

## 12. Структура проекта

```
/core
    CoreEngine.h / CoreEngine.cpp
    PipelineContext.h
    Interfaces.h
    Exceptions.h
/modules
    /capture
        ICapture.h
        DXGICapture.h / .cpp
        OpenGLCapture.h / .cpp
        VulkanCapture.h / .cpp
    /ocr
        IOCR.h
        TesseractOCR.h / .cpp
        PaddleOCR.h / .cpp
    /translator
        ITranslator.h
        LLMTranslator.h / .cpp
        MarianTranslator.h / .cpp
    /overlay
        IOverlay.h
        OpenGLOverlay.h / .cpp
        VulkanOverlay.h / .cpp
        WinAPIOverlay.h / .cpp
/plugins
    IPlugin.h
    PluginManager.h / .cpp
    PluginLoader.h / .cpp
/utils
    ThreadPool.h / .cpp
    LRUCache.h
    LatestQueue.h
    Logger.h / Logger.cpp
    Profiler.h / .cpp
    HotkeyManager.h / .cpp
    ConfigManager.h / .cpp
    TextTracker.h / .cpp
/config
    config.json
    default_config.json
/launcher (Python)
    main.py
    ui/
    requirements.txt
/tests
    test_*.cpp
/docs
    README.md
    API.md
CMakeLists.txt
```

---

## 13. Диаграмма классов

```
┌─────────────────────────────────────────────────────────────────┐
│                         CoreEngine                               │
│  - capture_: unique_ptr<ICapture>                               │
│  - ocr_: unique_ptr<IOCR>                                       │
│  - translator_: unique_ptr<ITranslator>                         │
│  - overlay_: unique_ptr<IOverlay>                               │
│  - context_: PipelineContext                                    │
│  - ocrBusy_: atomic<bool>                                       │
│  - translatorBusy_: atomic<bool>                                │
│  - threadPool_: ThreadPool                                      │
│  + captureLoop()                                                │
│  + ocrLoop()          ← throttle + pending frame               │
│  + translateLoop()    ← многопоточный                          │
│  + renderLoop()                                                │
│  + handleModuleError()                                          │
│  + recoverModule()                                              │
└──────────────────────────┬──────────────────────────────────────┘
                           │
           ┌───────────────┼───────────────┐
           ▼               ▼               ▼
    ┌────────────┐  ┌────────────┐  ┌────────────┐
    │ ICapture   │  │ IOCR       │  │ ITranslator│
    │ +getFrame()│  │ +process() │  │ +translate()│
    └─────┬──────┘  └─────┬──────┘  └─────┬──────┘
          │               │               │
    ┌─────┴──────┐  ┌─────┴──────┐  ┌─────┴──────┐
    │DXGICapture │  │TesseractOCR│  │LLMTranslator│
    │OpenGL      │  │PaddleOCR   │  │Marian      │
    │Vulkan      │  │             │  │Argos       │
    └────────────┘  └─────────────┘  └──────┬─────┘
                                            │
                                    ┌───────┴───────┐
                                    │ LRUCache      │
                                    │ (кеш переводов)│
                                    └───────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                    PipelineContext                               │
│  - frameQueue: LatestQueue<Frame>        ← throttle + pending  │
│  - textQueue: LatestQueue<vector<TextBlock>>                   │
│  - translatedQueue: LatestQueue<vector<TranslatedBlock>>       │
│  - captureTimestamp, ocrTimestamp, translatorTimestamp         │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                    Data Structures                               │
├─────────────────┬─────────────────┬─────────────────────────────┤
│ Frame           │ TextBlock       │ TranslatedBlock             │
│ - buffer        │ - id            │ - id                        │
│ - timestamp     │ - text          │ - originalText              │
│ - regions[]     │ - bbox          │ - translatedText            │
│ - sourceRect    │ - background    │ - bbox                      │
│ - format        │ - font          │ - style                     │
│                 │ - confidence    │ - confidence                │
└─────────────────┴─────────────────┴─────────────────────────────┘
```

---

## 14. Минимальный MVP

Для старта реализации:

1. **DXGICapture** — захват через DirectX (Windows)
2. **TesseractOCR** — распознавание текста
3. **ArgosTranslate** — перевод (легковесный)
4. **OpenGLOverlay** — отображение поверх приложения

---

## 15. Гарантии задержки (из ТЗ)

- **Максимальная задержка (P99):** ≤ 2 × (T_ocr + T_trans) + T_capture
- **Типичная задержка (P50):** ≈ T_ocr + T_trans + T_capture
- **Гарантия:** каждая начатая задача обязательно завершается, бесконечные отмены исключены

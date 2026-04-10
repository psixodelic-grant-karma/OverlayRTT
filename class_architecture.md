🧱 1. Общая архитектура (Core)
🔹 Базовый интерфейс модуля
```class IModule {
public:
    virtual ~IModule() = default;

    virtual bool init() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void process() = 0;
};
```
🔹 Расширенные интерфейсы (по ролям)
```class ICapture : public IModule {
public:
    virtual Frame getLatestFrame() = 0;
};

class IOCR : public IModule {
public:
    virtual std::vector<TextBlock> processFrame(const Frame& frame) = 0;
};

class ITranslator : public IModule {
public:
    virtual TranslatedBlock translate(const TextBlock& block) = 0;
};

class IOverlay : public IModule {
public:
    virtual void render(const std::vector<TranslatedBlock>& blocks) = 0;
};
```
📦 2. Основные структуры данных
```
struct Frame {
    ImageBuffer buffer;
    uint64_t timestamp;
};

struct TextBlock {
    int id;
    std::string text;
    Rect bbox;
    Color background;
    FontInfo font;
};

struct TranslatedBlock {
    int id;
    std::string translated_text;
    Rect bbox;
    Style style;
};
```
🔄 3. Очереди и обмен данными
```
Потокобезопасные очереди
template<typename T>
class ConcurrentQueue {
public:
    void push(const T& item);
    bool try_pop(T& item);
    void clear();
};
Глобальный Pipeline Context
class PipelineContext {
public:
    ConcurrentQueue<Frame> frameQueue;
    ConcurrentQueue<std::vector<TextBlock>> textQueue;
    ConcurrentQueue<std::vector<TranslatedBlock>> translatedQueue;
};
```
⚙️ 4. Реализация модулей
🎥 4.1 Capture Module
```
class DXGICapture : public ICapture {
private:
    Frame latestFrame;
    std::atomic<bool> running;

public:
    bool init() override;
    void start() override;
    void stop() override;

    void process() override;

    Frame getLatestFrame() override;
};
```
🔍 4.2 OCR Module
```
class TesseractOCR : public IOCR {
public:
    bool init() override;
    void start() override;
    void stop() override;

    void process() override;

    std::vector<TextBlock> processFrame(const Frame& frame) override;
};
```
🌍 4.3 Translator Module
```
class LLMTranslator : public ITranslator {
private:
    ThreadPool pool;

public:
    bool init() override;
    void start() override;
    void stop() override;

    void process() override;

    TranslatedBlock translate(const TextBlock& block) override;
};
```
🎨 4.4 Overlay Module
```
class OpenGLOverlay : public IOverlay {
public:
    bool init() override;
    void start() override;
    void stop() override;

    void process() override;

    void render(const std::vector<TranslatedBlock>& blocks) override;
};
```
🧠 5. Pipeline Orchestrator (ядро системы)
Главный управляющий класс
```
class Pipeline {
private:
    std::unique_ptr<ICapture> capture;
    std::unique_ptr<IOCR> ocr;
    std::unique_ptr<ITranslator> translator;
    std::unique_ptr<IOverlay> overlay;

    PipelineContext context;

    std::atomic<bool> running;

public:
    void init();
    void start();
    void stop();

private:
    void captureLoop();
    void ocrLoop();
    void translateLoop();
    void renderLoop();
};
```
Реализация потоков
🎥 Capture Loop
```
void Pipeline::captureLoop() {
    while (running) {
        Frame frame = capture->getLatestFrame();

        context.frameQueue.clear(); // оставляем только актуальный
        context.frameQueue.push(frame);
    }
}
```
🔍 OCR Loop
```
void Pipeline::ocrLoop() {
    while (running) {
        Frame frame;

        if (context.frameQueue.try_pop(frame)) {
            auto blocks = ocr->processFrame(frame);

            context.textQueue.clear();
            context.textQueue.push(blocks);
        }
    }
}
```
🌍 Translate Loop
```
void Pipeline::translateLoop() {
    while (running) {
        std::vector<TextBlock> blocks;

        if (context.textQueue.try_pop(blocks)) {
            std::vector<TranslatedBlock> results;

            for (auto& b : blocks) {
                results.push_back(translator->translate(b));
            }

            context.translatedQueue.clear();
            context.translatedQueue.push(results);
        }
    }
}
```
🎨 Render Loop
```
void Pipeline::renderLoop() {
    while (running) {
        std::vector<TranslatedBlock> blocks;

        if (context.translatedQueue.try_pop(blocks)) {
            overlay->render(blocks);
        }
    }
}
```
🔌 6. Плагинная система
```
Интерфейс загрузчика
class PluginLoader {
public:
    template<typename T>
    static std::unique_ptr<T> load(const std::string& path);
};
```
Пример загрузки
```
capture = PluginLoader::load<ICapture>("dxgi_capture.dll");
ocr = PluginLoader::load<IOCR>("tesseract_ocr.dll");
translator = PluginLoader::load<ITranslator>("llm_translator.dll");
overlay = PluginLoader::load<IOverlay>("opengl_overlay.dll");
```
🧰 7. Конфигурация
```
struct AppConfig {
    CaptureConfig capture;
    OCRConfig ocr;
    TranslatorConfig translator;
    OverlayConfig overlay;
};
```
⚡ 8. Оптимизации (ВАЖНО)
🔹 1. Frame Deduplication
```
if (frame.timestamp == lastFrame.timestamp)
    continue;
    
```
🔹 2. OCR Cache
```
std::unordered_map<std::string, std::string> translationCache;
```
🔹 3. Text Tracking
```
class TextTracker {
public:
    bool isChanged(const TextBlock& block);
};
```
🔹 4. ThreadPool
```
class ThreadPool {
public:
    void enqueue(std::function<void()> task);
};
```
📁 9. Структура проекта
```
/core
    Pipeline.h
    Pipeline.cpp
    Interfaces.h

/modules
    /capture
        DXGI.cpp
        Vulkan.cpp
    /ocr
        Tesseract.cpp
    /translator
        LLM.cpp
    /overlay
        OpenGL.cpp

/plugins
    loader.cpp

/utils
    ThreadPool.cpp
    ConcurrentQueue.cpp

/config
    config.json

/launcher (Python)
```

🚀 10. Минимальный MVP (реально начать)
```
DXGICapture
TesseractOCR
ArgosTranslate
OpenGLOverlay
```

``
Общие принципы
Модульность через интерфейсы – каждый блок (Capture, OCR, Translator, Overlay) реализует абстрактный класс плагина.

Асинхронный конвейер – потокобезопасные очереди сообщений (Frame, TextBlock, TranslatedBlock).

Паттерны: Стратегия (выбор бэкенда/движка), Фабрика (создание плагинов), Наблюдатель (нотификации о состоянии), Очередь (Producer-Consumer).

Управление ресурсами – RAII, интеллектуальные указатели, пулы памяти для изображений.

Диаграмма классов (основные сущности)
text
```
┌─────────────────────────┐      ┌──────────────────────┐
│      CoreEngine         │      │   PluginManager      │
│  - queues               │◄────►│ - loadPlugin()       │
│  - threads              │      │ - getCapturePlugins()│
│  - config               │      └──────────────────────┘
└───────────┬─────────────┘               ▲
            │                               │
            ▼                               │
┌─────────────────────────┐      ┌─────────┴─────────┐
│   MessageQueue<T>       │      │   IPlugin         │
│  - push() non-blocking  │      │ + init()          │
│  - pop()                │      │ + start()/stop()  │
│  - tryPop()             │      │ + getType()       │
└─────────────────────────┘      └─────────┬─────────┘
                                            │
              ┌─────────────────────────────┼─────────────────────────────┐
              │                             │                             │
              ▼                             ▼                             ▼
     ┌─────────────────┐          ┌─────────────────┐          ┌─────────────────┐
     │ ICapturePlugin  │          │  IOCRPlugin     │          │ ITranslatorPlugin│
     │ + startCapture()│          │ + processFrame()│          │ + translate()   │
     │ + getFrame()    │          │ + setLanguage() │          │ + setLanguages()│
     └────────┬────────┘          └────────┬────────┘          └────────┬────────┘
              │                            │                            │
     ┌────────┴────────┐          ┌────────┴────────┐          ┌────────┴────────┐
     │ DXGICapture     │          │ TesseractOCR    │          │ LLMTranslator   │
     │ OpenGLCapture   │          │ PaddleOCR       │          │ MarianTranslator│
     │ VulkanCapture   │          │ ...             │          │ ...             │
     └─────────────────┘          └─────────────────┘          └─────────────────┘

           ┌─────────────────┐
           │ IOverlayPlugin  │
           │ + renderBlocks()│
           │ + setVisible()  │
           └────────┬────────┘
                    │
           ┌────────┴────────┐
           │ OpenGLOverlay   │
           │ VulkanOverlay   │
           │ WinAPIOverlay   │
           └─────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                      Data Structures                             │
├─────────────────┬─────────────────┬─────────────────────────────┤
│ Frame           │ TextBlock       │ TranslatedBlock              │
│ - buffer        │ - text          │ - blockId                    │
│ - timestamp     │ - bbox (rect)   │ - originalText               │
│ - sourceRect    │ - background    │ - translatedText             │
│ - format        │ - fontInfo      │ - confidence                 │
└─────────────────┴─────────────────┴─────────────────────────────┘
```
Детальное описание классов
1. Ядро и инфраструктура
CoreEngine
Ответственность: запуск конвейера, управление очередями, координация потоков.

Члены:

MessageQueue<Frame> captureQueue

MessageQueue<TextBlock> ocrQueue

MessageQueue<TranslatedBlock> translationQueue

vector<unique_ptr<IPlugin>> plugins

ConfigManager config

ThreadPool workerPool (для параллельной обработки блоков)

Методы:

run() – основной цикл, запускает потоки модулей.

stop() – остановка и освобождение ресурсов.

notifyModuleState(ModuleType, State) – для обработки ошибок.

MessageQueue<T>
Шаблонный класс с lockfree::queue (moodycamel) или std::queue + мьютекс.

Особенности:

push(T&& item, bool dropIfFull = true) – при переполнении сбрасывает старые.

tryPop(T& out) – неблокирующий.

getLast() – получить последний элемент без удаления (для самого свежего кадра).

ConfigManager
Загрузка/сохранение конфигурации в JSON (например, nlohmann/json).

Хранение настроек для каждого плагина.

2. Система плагинов
IPlugin (абстрактный базовый класс)
cpp
class IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual bool init(const Config& cfg) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual PluginType getType() const = 0;
    virtual std::string getName() const = 0;
};
PluginManager
Функции:

loadPluginsFromDir(const fs::path& dir) – динамическая загрузка .dll / .so.

registerPlugin(std::shared_ptr<IPlugin>) – для статически связанных плагинов.

getPluginByType<Interface>(PluginType) – получение экземпляра.

Хранение: unordered_map<PluginType, vector<shared_ptr<IPlugin>>>

3. Модуль захвата экрана (Capture)
ICapturePlugin : public IPlugin
cpp
class ICapturePlugin : public IPlugin {
public:
    virtual bool setCaptureSource(const CaptureSource& source) = 0;
    virtual bool setIgnoreRegions(const vector<Rect>& regions) = 0;
    virtual void setCaptureMode(CaptureMode mode) = 0; // Callback, Timer, Hybrid
    virtual void setCallback(std::function<void(const Frame&)> onFrame) = 0;
    virtual void startCapture() = 0;
    virtual void stopCapture() = 0;
};
Конкретные реализации:

DXGICapture – Windows, DirectX 11/12.

OpenGLCapture – кроссплатформа, через glReadPixels + PBO для асинхронности.

VulkanCapture – использование VkInstance + захват через VkBuffer.

ScreenCaptureLite – обёртка над библиотекой screen_capture_lite.

Вспомогательные структуры:

cpp
struct CaptureSource {
    enum Type { FullScreen, Window, Region };
    Type type;
    union {
        HWND hwnd;        // Window handle
        Rect regionRect;  // для Region
    };
};
enum class CaptureMode { Callback, Timer, Hybrid };
4. Модуль OCR
IOCRPlugin : public IPlugin
cpp
class IOCRPlugin : public IPlugin {
public:
    virtual void setLanguages(const std::vector<std::string>& langs) = 0;
    virtual void setPreprocessingFilters(const std::vector<Filter>& filters) = 0;
    virtual std::vector<TextBlock> processFrame(const Frame& frame) = 0;
    virtual void enableTextTracking(bool enable) = 0; // избегать повторной обработки
};
Конкретные реализации:

TesseractOCR – обёртка над tesseract::TessBaseAPI.

PaddleOCR – использование C++ API PaddlePaddle.

TextTracking – хранит хэш предыдущих блоков (позиция + текст) и не отправляет на перевод неизменённые.

Структура TextBlock:

cpp
struct TextBlock {
    std::string text;
    Rect bbox;
    Color background;      // средний цвет фона
    FontInfo font;         // размер, гарнитура (опционально)
    int blockId;           // уникальный ID на кадре
    float confidence;
};
5. Модуль перевода
ITranslatorPlugin : public IPlugin
cpp
class ITranslatorPlugin : public IPlugin {
public:
    virtual void setSourceLanguage(const std::string& lang) = 0; // "auto" или код
    virtual void setTargetLanguage(const std::string& lang) = 0;
    virtual void setInferenceParams(const InferenceParams& params) = 0;
    virtual std::string translate(const std::string& text, int blockId) = 0;
    virtual void setCacheSize(size_t size) = 0; // кэш переводов
};
Конкретные реализации:

LLaMCPPTranslator – интеграция с llama.cpp.

MarianTranslator – использование libmarian.

ArgosTranslate – легковесный вариант.

Кэш переводов – LRUCache<std::string, std::string> (исходный текст → перевод).

Параллельный перевод – пул потоков внутри плагина, каждому блоку выделяется задача.

6. Модуль оверлея
IOverlayPlugin : public IPlugin
cpp
class IOverlayPlugin : public IPlugin {
public:
    virtual void setBackgroundMode(BackgroundMode mode) = 0; // Fixed, Auto, Gradient, Inpaint
    virtual void setTextStyle(const TextStyle& style) = 0;
    virtual void setAutoFontMatch(bool enable) = 0;
    virtual void render(const std::vector<TranslatedBlock>& blocks, const Frame& originalFrame) = 0;
    virtual void setHotkeys(const std::vector<Hotkey>& keys) = 0;
    virtual void setVisible(bool visible) = 0;
    virtual void setOutputWindow(bool useSeparateWindow) = 0;
};
Конкретные реализации:

OpenGLOverlay – прозрачное окно с контекстом OpenGL, рендер через текстурные квады.

VulkanOverlay – для интеграции с играми на Vulkan.

WinAPIOverlay – простой WS_EX_LAYERED с GDI (медленнее, но минимально).

BackgroundMode – вычисляется в зависимости от режима:

Auto: из originalFrame по координатам блока вычисляется средний цвет.

Gradient: два средних цвета (левая/правая часть блока) и интерполяция.

Inpaint: использует алгоритм inpainting (например, из OpenCV) для удаления исходного текста.

TextStyle:

cpp
struct TextStyle {
    Color color;
    std::string fontFamily;
    int fontSize;
    bool bold, italic;
    float outline; // обводка
};
7. Лаунчер (отдельный процесс на Python)
Не входит в C++ архитектуру, но взаимодействует через:

IPC (например, named pipes или socket) для отправки команд (старт/стоп, изменение настроек).

Файл конфигурации (общий config.json), который читает C++ CoreEngine.

Лаунчер управляет плагинами через PluginManager (может вызывать его методы через IPC). Для простоты можно сделать так, что лаунчер просто пишет конфиг и запускает C++ процесс, а процесс сам подхватывает изменения по сигналу или перезагрузке.

8. Дополнительные классы
Logger
Асинхронное логирование с уровнями.

Поддержка ротации файлов.

Profiler
Замер задержек между этапами (capture → OCR → translation → render).

Отправка метрик в GUI.

HotkeyManager
Глобальный захват горячих клавиш (Windows: RegisterHotKey, Linux: XGrabKey).

ThreadPool
Для параллельной обработки блоков в модулях OCR и Translator.

Взаимодействие классов (последовательность работы)
Запуск: CoreEngine инициализирует PluginManager, загружает выбранные плагины.

Конфигурация: через лаунчер или config.json настраиваются параметры каждого плагина.

Цикл захвата:

ICapturePlugin по таймеру/callback получает кадр, кладёт в captureQueue.

Поток OCR читает из captureQueue (только последний), вызывает processFrame(), результат (список TextBlock) кладёт в ocrQueue.

Поток Translator читает ocrQueue, для каждого блока вызывает translate() (параллельно через ThreadPool), создаёт TranslatedBlock, кладёт в translationQueue.

Поток Overlay читает translationQueue и вызывает render().

Обработка ошибок: любой плагин при сбое отправляет нотификацию в CoreEngine, который может перезапустить модуль или отключить его.``
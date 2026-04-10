# Real-Time Screen Translator

Кроссплатформенное приложение для экранного перевода в реальном времени.

## Возможности

- Захват экрана (Windows/Linux/macOS)
- OCR распознавание текста
- Локальный перевод (без интернета)
- Отображение перевода поверх приложения

## Требования

### Системные зависимости

#### Ubuntu/Debian
```bash
sudo apt install build-essential cmake pkg-config
sudo apt install libx11-dev libxext-dev libxfixes-dev libice-dev
sudo apt install libxcb1-dev libxcb-render0-dev libxcb-shape0-dev
sudo apt install libtesseract-dev libleptonica-dev
sudo apt install libavformat-dev libavcodec-dev libswscale-dev libavutil-dev libavdevice-dev
sudo apt install libcurl4-openssl-dev python3-dev
```

Или используйте готовый requirements.txt:
```bash
sudo apt install $(cat requirements.txt | grep -v "^#" | tr "\n" " ")
```

#### Arch Linux
```bash
sudo pacman -S base-devel cmake pkgconf
sudo pacman -S libx11 libxext libxfixes libice
sudo pacman -S libxcb
sudo pacman -S tesseract leptonica
sudo pacman -S ffmpeg
sudo pacman -S curl python
```

## Сборка

```bash
# Клонирование репозитория
git clone <repo_url>
cd real_time_translator

# Создание директории сборки
mkdir build && cd build

# Конфигурация
cmake .. -DCMAKE_BUILD_TYPE=Release

# Сборка
make -j4

# Portable сборка будет в:
# build/translator/
```

## Скачивание модели для перевода

Для работы переводчика необходимо скачать модель:

```bash
# Автоматически
chmod +x scripts/download_bergamot_model.sh
./scripts/download_bergamot_model.sh

# Или вручную:
cd 3rd_party/models
wget https://data.statmt.org/bergamot/models/deen/ende.student.tiny11.v2.93821e13b3c511b5.tar.gz
tar -xzf ende.student.tiny11.v2.93821e13b3c511b5.tar.gz
# Затем добавить .bergamot.yml к config файлу
```

## Запуск

После сборки portable версия находится в папке `build/translator/`:

```bash
cd build/translator
./translator_app --verbose
```

## Структура проекта

```
real_time_translator/
├── 3rd_party/              # Сторонние библиотеки
│   ├── bergamot/          # Bergamot переводчик
│   ├── models/            # Модели перевода
│   └── screen_capture_lite/ # Захват экрана
├── core/                   # Ядро приложения
├── modules/                # Модули (capture, ocr, translator, overlay)
├── utils/                  # Утилиты
├── config/                 # Конфигурация
├── scripts/                # Скрипты
├── build/                  # Сборка
│   └── translator/        # Portable версия
├── CMakeLists.txt
├── requirements.txt
└── README.md
```

## Использование

### Горячие клавиши

- `Ctrl+Shift+T` - Включить/выключить перевод
- `Ctrl+Shift+R` - Перезапуск захвата

### Конфигурация

Отредактируйте `config/default_config.json` для настройки:
- Язык перевода
- Частота захвата
- Размер кэша

## Лицензия

MIT

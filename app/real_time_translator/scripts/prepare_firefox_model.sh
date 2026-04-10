#!/bin/bash
# Скрипт для подготовки Firefox моделей для Bergamot
# Запускать из корневой папки проекта

set -e

SOURCE_LANG="en"
TARGET_LANG="ru"
MODEL_DIR="3rd_party/models/enru"

echo "=== Preparing Firefox model for Bergamot ==="

# Создаем директорию
mkdir -p "$MODEL_DIR"
cd "$MODEL_DIR"

# Копируем модель из firefox-translations-models
cd "$(dirname "$0")/../.."
SOURCE_DIR="$(pwd)/firefox-translations-models/models/base/${SOURCE_LANG}${TARGET_LANG}"
cd "$(dirname "$0")/../3rd_party/models/enru"

if [ -f "${SOURCE_DIR}/model.enru.intgemm.alphas.bin.gz" ]; then
    echo "Copying model files from ${SOURCE_DIR}..."
    cp "${SOURCE_DIR}"/*.gz ./
    
    # Распаковываем
    echo "Unpacking..."
    gunzip -f *.gz 2>/dev/null || true
else
    echo "Model not found: ${SOURCE_DIR}"
    ls -la "${SOURCE_DIR}" 2>/dev/null || echo "Directory does not exist"
    exit 1
fi

# Создаем конфиг для bergamot
echo "Creating bergamot config..."

cat > config.yml << EOF
models:
  - model.${SOURCE_LANG}${TARGET_LANG}.intgemm.alphas.bin
vocabs:
  - vocab.${SOURCE_LANG}${TARGET_LANG}.spm
  - vocab.${SOURCE_LANG}${TARGET_LANG}.spm
shortlist:
  - lex.50.50.${SOURCE_LANG}${TARGET_LANG}.s2t.bin
  - 50
  - 50
quality:
  - qualityModel.${SOURCE_LANG}${TARGET_LANG}.bin
EOF

# Переименовываем файлы
if [ -f "model.${SOURCE_LANG}${TARGET_LANG}.intgemm.alphas.bin" ]; then
    mv "model.${SOURCE_LANG}${TARGET_LANG}.intgemm.alphas.bin" "model.bin"
fi
if [ -f "vocab.${SOURCE_LANG}${TARGET_LANG}.spm" ]; then
    mv "vocab.${SOURCE_LANG}${TARGET_LANG}.spm" "vocab.spm"
fi
if [ -f "lex.50.50.${SOURCE_LANG}${TARGET_LANG}.s2t.bin" ]; then
    mv "lex.50.50.${SOURCE_LANG}${TARGET_LANG}.s2t.bin" "lex.s2t.bin"
fi

# Обновляем конфиг
cat > config.yml << EOF
models:
  - model.bin
vocabs:
  - vocab.spm
  - vocab.spm
shortlist:
  - lex.s2t.bin
  - 50
  - 50
quality: []
EOF

echo "=== Model ready ==="
ls -la
echo ""
echo "Config:"
cat config.yml

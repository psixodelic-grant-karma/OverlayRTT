#!/bin/bash
# Скрипт для скачивания и подготовки модели Bergamot
# Запускать из корневой папки проекта

set -e

MODELS_DIR="3rd_party/models"
mkdir -p "$MODELS_DIR"

echo "=== Downloading Bergamot model (de-en, tiny) ==="
cd "$MODELS_DIR"

# Скачиваем модель
if [ ! -f "ende.student.tiny11.v2.93821e13b3c511b5.tar.gz" ]; then
    wget --quiet --continue \
        https://data.statmt.org/bergamot/models/deen/ende.student.tiny11.v2.93821e13b3c511b5.tar.gz
fi

# Распаковываем
if [ ! -d "ende.student.tiny11" ]; then
    tar -xzf ende.student.tiny11.v2.93821e13b3c511b5.tar.gz
fi

# Патчим конфиг для bergamot
echo "=== Patching config for Bergamot ==="
cd "ende.student.tiny11"

# Определяем путь к ssplit prefix file
SSPLIT_PREFIX=""
if [ -f "../../../../bergamot-translator/3rd_party/ssplit-cpp/nonbreaking_prefixes/nonbreaking_prefix.en" ]; then
    SSPLIT_PREFIX="../../../../bergamot-translator/3rd_party/ssplit-cpp/nonbreaking_prefixes/nonbreaking_prefix.en"
elif [ -f "/usr/share/ssplit/nonbreaking_prefix.en" ]; then
    SSPLIT_PREFIX="/usr/share/ssplit/nonbreaking_prefix.en"
else
    echo "Warning: ssplit prefix file not found, using empty"
    SSPLIT_PREFIX=""
fi

# Патчим конфиг
python3 -c "
import sys
import yaml

config_file = 'config.intgemm8bitalpha.yml'
output_file = config_file + '.bergamot.yml'

with open(config_file, 'r') as f:
    config = yaml.safe_load(f)

# Добавляем необходимые поля для bergamot
config['bergamot'] = {}
config['vocab'] = {'source': 'sentencepiece.bpe.model', 'target': 'sentencepiece.bpe.model'}
config['quality'] = []

if '$SSPLIT_PREFIX':
    config['preprocess'] = {'ssplit': {'mode': 'wrapper', 'ssplit-prefix-file': '$SSPLIT_PREFIX'}}

with open(output_file, 'w') as f:
    yaml.dump(config, f, default_flow_style=False)

print(f'Created {output_file}')
" || echo "Manual patch needed: add .bergamot.yml suffix to config file"

echo "=== Model ready ==="
echo "Config file: $MODELS_DIR/ende.student.tiny11/config.intgemm8bitalpha.yml.bergamot.yml"

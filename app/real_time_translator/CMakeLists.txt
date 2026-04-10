cmake_minimum_required(VERSION 3.16)
project(RealTimeTranslator VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Опции сборки
option(BUILD_TESTS "Build tests" OFF)
option(BUILD_LAUNCHER "Build Python launcher" OFF)

# Директории поиска зависимостей
set(CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/deps" CACHE PATH "Prefix path for dependencies")

# Пути к 3rd_party
set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/3rd_party")
set(SCREEN_CAPTURE_LITE_DIR "${THIRD_PARTY_DIR}/screen_capture_lite")
set(BERGAMOT_DIR "${THIRD_PARTY_DIR}/bergamot")

# Основные директории
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/modules
    ${CMAKE_SOURCE_DIR}/utils
    ${CMAKE_SOURCE_DIR}/config
)

# Компиляторные флаги
if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -pedantic)
endif()

# Поиск библиотек
find_package(Threads REQUIRED)
find_package(nlohmann_json 3.10.5 QUIET)

# Tesseract OCR - ищем через pkg-config, но без leptonica
find_package(PkgConfig REQUIRED)
pkg_check_modules(TESSERACT REQUIRED tesseract)
include_directories(${TESSERACT_INCLUDE_DIRS})

# FFmpeg для захвата экрана
find_package(PkgConfig)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libswscale libavutil libavdevice)
include_directories(${FFMPEG_INCLUDE_DIRS})

# Python для CTranslate2 переводчика (опционально)
find_package(Python3 COMPONENTS Interpreter Development)
if(Python3_FOUND)
    include_directories(${Python3_INCLUDE_DIRS})
endif()

# CURL для Ollama переводчика
find_package(CURL)

# Bergamot Translator (subprocess вызов, библиотеки для совместимости)
if(EXISTS ${BERGAMOT_DIR}/libmarian.a)
    set(BERGAMOT_LIBRARIES "${BERGAMOT_DIR}/libmarian.a" "${BERGAMOT_DIR}/libssplit.a")
    message(STATUS "Found Bergamot libraries: ${BERGAMOT_LIBRARIES}")
else()
    set(BERGAMOT_LIBRARIES "")
endif()

# Исходные файлы ядра
set(CORE_SOURCES
    core/CoreEngine.cpp
    core/Interfaces.cpp
)

# Утилиты
set(UTILS_SOURCES
    utils/LatestQueue.cpp
    utils/ThreadPool.cpp
    utils/Logger.cpp
    utils/ConfigManager.cpp
)

# Модули stub-реализации
set(MODULES_SOURCES
    modules/capture/NullCapture.h
    modules/capture/GrimCapture.h
    modules/ocr/StubOCR.h
    modules/translator/StubTranslator.h
    modules/translator/BergamotTranslator.h
    modules/overlay/StubOverlay.h
)

# Сборка библиотеки ядра
add_library(translator_core STATIC
    ${CORE_SOURCES}
    ${UTILS_SOURCES}
    ${MODULES_SOURCES}
)

target_link_libraries(translator_core
    Threads::Threads
    "${SCREEN_CAPTURE_LITE_DIR}/libscreen_capture_lite_shared.so"
    X11
    ICE
    xcb
    xcb-render
    xcb-shape
    ${TESSERACT_LIBRARIES}
    leptonica
    ${FFMPEG_LIBRARIES}
    Xfixes
    Xext
)

if(nlohmann_json_FOUND)
    target_link_libraries(translator_core nlohmann_json::nlohmann_json)
endif()

# Главный исполняемый файл
add_executable(translator_app
    main.cpp
)

target_link_libraries(translator_app
    translator_core
    ${Python3_LIBRARIES}
    CURL::libcurl
    ${BERGAMOT_LIBRARIES}
)

# Настройка portable сборки
if(NOT MSVC)
    # Папка для portable сборки (рядом с exe)
    set(PORTABLE_DIR ${CMAKE_CURRENT_BINARY_DIR}/translator)
    file(MAKE_DIRECTORY ${PORTABLE_DIR})
    
    # Копируем исполняемый файл
    add_custom_command(TARGET translator_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:translator_app>
            ${PORTABLE_DIR}/
    )
    
    # Копируем библиотеку screen_capture_lite
    if(EXISTS ${SCREEN_CAPTURE_LITE_DIR}/libscreen_capture_lite_shared.so)
        add_custom_command(TARGET translator_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SCREEN_CAPTURE_LITE_DIR}/libscreen_capture_lite_shared.so"
                ${PORTABLE_DIR}/
        )
    endif()
    
    # Копируем bergamot бинарник если есть
    if(EXISTS ${BERGAMOT_DIR}/bergamot)
        add_custom_command(TARGET translator_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${BERGAMOT_DIR}/bergamot
                ${PORTABLE_DIR}/
        )
        
        # Копируем модели bergamot если есть
        if(EXISTS ${THIRD_PARTY_DIR}/models)
            add_custom_command(TARGET translator_app POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${THIRD_PARTY_DIR}/models
                    ${PORTABLE_DIR}/models
            )
        endif()
    endif()
    
    # Копируем конфигурацию
    add_custom_command(TARGET translator_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/config/default_config.json
            ${PORTABLE_DIR}/
    )
    
    # Устанавливаем rpath для поиска библиотек в папке приложения
    set_target_properties(translator_app PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
    
    message(STATUS "Portable build will be in: ${PORTABLE_DIR}")
endif()

# Тесты
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Установка
install(TARGETS translator_app
    RUNTIME DESTINATION bin
)

install(FILES config/default_config.json
    DESTINATION share/translator
)

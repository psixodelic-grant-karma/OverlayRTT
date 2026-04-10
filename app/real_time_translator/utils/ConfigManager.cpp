#include "ConfigManager.h"

#if HAS_NLOHMANN_JSON
#include <fstream>
#endif

namespace translator {

ConfigManager::ConfigManager() = default;

ConfigManager::~ConfigManager() = default;

bool ConfigManager::load(const std::filesystem::path& path) {
#if HAS_NLOHMANN_JSON
    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            // Файл не существует - создаём по умолчанию
            config_ = nlohmann::json::object();
            configPath_ = path;
            return save(path);
        }
        
        file >> config_;
        configPath_ = path;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ConfigManager] Failed to load config: " << e.what() << std::endl;
        return false;
    }
#else
    configPath_ = path;
    return false;
#endif
}

bool ConfigManager::save(const std::filesystem::path& path) {
#if HAS_NLOHMANN_JSON
    try {
        std::filesystem::path savePath = path.empty() ? configPath_ : path;
        
        // Создаём директорию если нужно
        if (auto parent = savePath.parent_path(); !parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        
        std::ofstream file(savePath);
        if (!file.is_open()) {
            std::cerr << "[ConfigManager] Failed to open file for writing: " << savePath << std::endl;
            return false;
        }
        
        file << config_.dump(4);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ConfigManager] Failed to save config: " << e.what() << std::endl;
        return false;
    }
#else
    return false;
#endif
}

bool ConfigManager::has(const std::string& key) const {
#if HAS_NLOHMANN_JSON
    try {
        nlohmann::json::json_pointer ptr(key);
        return config_.contains(ptr);
    } catch (...) {
        return false;
    }
#else
    return false;
#endif
}

std::string ConfigManager::toString() const {
#if HAS_NLOHMANN_JSON
    return config_.dump(4);
#else
    return "{}";
#endif
}

void ConfigManager::subscribe(std::function<void()> callback) {
    onChangeCallback_ = callback;
}

bool ConfigManager::reload() {
    if (configPath_.empty()) return false;
    return load(configPath_);
}

} // namespace translator

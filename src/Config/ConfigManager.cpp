#include "Config/ConfigManager.h"

#include "Config/ConfigSerialization.h"
#include "logger.h"

#include <fstream>
#include <iterator>

namespace IPA {

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

ConfigManager::ConfigManager() : mConfig(std::make_unique<Config>()) {}

ConfigManager::~ConfigManager() = default;

bool ConfigManager::load(const std::string& path) {
    mConfigPath = path;

    std::ifstream file(path);
    if (!file.is_open()) {
        // 文件不存在，创建默认配置
        logger.info("Config file not found, creating a new one at: {}", path);
        return save();
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    nlohmann::json user_json;
    try {
        // 解析用户配置，允许注释
        user_json = nlohmann::json::parse(content, nullptr, true, true);
        // 填充内存中的配置对象。缺失的键将从结构体定义中获取默认值。
        *mConfig = user_json.get<Config>();
    } catch (const nlohmann::json::exception& e) {
        logger.error("Failed to parse config file: {}. Please check for syntax errors.", e.what());
        return false;
    }

    // 应用日志等级
    logger.setLevel(static_cast<ll::io::LogLevel>(mConfig->logLevel));

    return true; // 不再保存，以保留用户文件的格式和注释
}

bool ConfigManager::save() {
    try {
        nlohmann::json json = *mConfig;
        std::ofstream  file(mConfigPath);
        file << json.dump(4);
    } catch (const nlohmann::json::exception& e) {
        logger.error("Failed to save config file: {}", e.what());
        return false;
    }
    return true;
}

bool ConfigManager::reload() {
    if (mConfigPath.empty()) {
        logger.warn("Reload requested before config path was initialized.");
        return false;
    }
    if (!load(mConfigPath)) {
        return false;
    }
    return true;
}

Config&       ConfigManager::get() { return *mConfig; }
const Config& ConfigManager::get() const { return *mConfig; }

} // namespace IPA

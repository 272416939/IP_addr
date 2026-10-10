#include "Entry/Entry.h"

#include "Command/Command.h"
#include "Config/ConfigManager.h"
#include "Network/IpService.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerConnectEvent.h"
#include "ll/api/mod/RegisterHelper.h"
#include "logger.h"

#include <filesystem>

namespace IPA {

namespace {

// 从 RakNet 的 "IP|端口" 字符串中提取纯 IP 地址
std::string extractIp(std::string const& ipAndPort) {
    std::string result = ipAndPort;

    // RakNet 通常使用 '|' 分隔 IP 与端口，例如 "1.2.3.4|12345"
    auto pipe = result.find('|');
    if (pipe != std::string::npos) {
        return result.substr(0, pipe);
    }

    // IPv6 可能带有方括号，例如 "[::1]:19132"
    if (!result.empty() && result.front() == '[') {
        auto close = result.find(']');
        if (close != std::string::npos) {
            return result.substr(1, close - 1);
        }
    }

    // 若只有一个冒号，视为 "IPv4:端口"
    if (result.find(':') != std::string::npos && result.find(':') == result.rfind(':')) {
        return result.substr(0, result.find(':'));
    }

    return result; // 纯 IPv6 地址，原样返回
}

// 玩家连接服务器（登录阶段）时触发，此时可以拿到玩家的网络标识
void onPlayerConnect(ll::event::PlayerConnectEvent& event) {
    const auto& config = ConfigManager::getInstance().get();

    auto&       player   = event.self();
    std::string playerIp = extractIp(event.networkIdentifier().getIPAndPort());

    std::string error;
    auto        info = IpService::queryIp(playerIp, config.key, config.timeoutMs, error);
    if (info) {
        logger.info(
            "玩家 {} 加入游戏 IP: {} 运营商: {} 地理位置: {}",
            player.getRealName(),
            info->ip.empty() ? playerIp : info->ip,
            info->isp.empty() ? "未知" : info->isp,
            info->region.empty() ? "未知" : info->region
        );
    } else {
        logger.warn("玩家 {} 加入游戏 IP: {} 地理位置查询失败: {}", player.getRealName(), playerIp, error);
    }
}

} // namespace

Entry& Entry::getInstance() {
    static Entry instance;
    return instance;
}

Entry::Entry() : mSelf(*ll::mod::NativeMod::current()) {}

ll::mod::NativeMod& Entry::getSelf() const { return mSelf; }

bool Entry::load() {
    getSelf().getLogger().debug("Loading...");

    auto configDir = getSelf().getConfigDir();
    if (!std::filesystem::exists(configDir)) {
        std::filesystem::create_directories(configDir);
    }
    auto configPath = configDir / "config.json";
    configPath.make_preferred();

    if (!ConfigManager::getInstance().load(configPath.string())) {
        getSelf().getLogger().error("Failed to load config file!");
        return false;
    }
    return true;
}

bool Entry::enable() {
    getSelf().getLogger().debug("Enabling...");

    const auto& config = ConfigManager::getInstance().get();

    // 应用日志等级
    getSelf().getLogger().setLevel(static_cast<ll::io::LogLevel>(config.logLevel));

    // 未配置 API Key 时提示当前使用免费额度
    if (config.key.empty()) {
        getSelf().getLogger().warn(
            "未配置 API Key，当前使用免费额度，请尽快配置 key（配置文件: plugins/IP_addr/config.json）"
        );
    } else {
        getSelf().getLogger().info("API Key 已配置，将使用该 key 查询 IP 信息");
    }

    registerCommand();

    if (config.enableJoinQuery) {
        auto& eventBus = ll::event::EventBus::getInstance();
        eventBus.emplaceListener<ll::event::PlayerConnectEvent>(onPlayerConnect);
    }

    return true;
}

bool Entry::disable() {
    getSelf().getLogger().debug("Disabling...");
    return true;
}

bool Entry::unload() {
    getSelf().getLogger().debug("Unloading...");
    return true;
}

} // namespace IPA

LL_REGISTER_MOD(IPA::Entry, IPA::Entry::getInstance());

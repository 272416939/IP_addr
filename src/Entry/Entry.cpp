#include "Entry/Entry.h"

#include "Command/Command.h"
#include "Config/ConfigManager.h"
#include "Db/IpDatabase.h"
#include "Network/IpService.h"
#include "Network/IpUtils.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerConnectEvent.h"
#include "ll/api/mod/RegisterHelper.h"
#include "logger.h"

#include <filesystem>

namespace IPA {

namespace {

// 输出一条玩家进服 IP 信息日志
void writeJoinLog(Player& player, const IpInfo& info, const std::string& fallbackIp, const char* suffix) {
    logger.info(
        "玩家 {} 加入游戏 IP: {} 运营商: {} 地理位置: {}{}",
        player.getRealName(),
        info.ip.empty() ? fallbackIp : info.ip,
        info.isp.empty() ? "未知" : info.isp,
        info.region.empty() ? "未知" : info.region,
        suffix
    );
}

// 玩家连接服务器（登录阶段）时触发，此时可以拿到玩家的网络标识
void onPlayerConnect(ll::event::PlayerConnectEvent& event) {
    const auto& config = ConfigManager::getInstance().get();

    auto&       player   = event.self();
    std::string uuid     = player.getUuid().asString();
    std::string playerIp = IpUtils::extractIp(event.networkIdentifier().getIPAndPort());

    // 内网地址：不请求 API，直接显示内网信息
    if (IpUtils::isPrivateIp(playerIp)) {
        writeJoinLog(player, IpUtils::makePrivateIpInfo(playerIp), playerIp, "");
        return;
    }

    auto& db = IpDatabase::getInstance();

    // 缓存：同一玩家 IP 未变化时直接读数据库，不请求 API
    if (auto cached = db.getCachedForPlayer(uuid, playerIp)) {
        writeJoinLog(player, *cached, playerIp, " (缓存)");
        return;
    }

    // IP 发生变化（或首次进服）：请求 API 获取新 IP 的信息（失败时按配置自动重试）
    std::string error;
    auto        info = IpService::queryIp(
        playerIp,
        config.key,
        config.timeoutMs,
        config.retryCount,
        config.retryDelayMs,
        error
    );
    if (info) {
        db.savePlayerResult(uuid, playerIp, *info);
        writeJoinLog(player, *info, playerIp, "");
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

    // 打开 SQLite 缓存数据库（失败时降级为无缓存运行，不影响 IP 查询主功能）
    const auto& config = ConfigManager::getInstance().get();
    auto        dbPath = std::filesystem::path(config.databasePath);
    std::error_code ec;
    std::filesystem::create_directories(dbPath.parent_path(), ec);
    if (ec) {
        getSelf().getLogger().warn("创建数据库目录失败: {} ({})", dbPath.parent_path().string(), ec.message());
    }
    if (!IpDatabase::getInstance().open(dbPath.string())) {
        getSelf().getLogger().error("打开 IP 缓存数据库失败，将在无缓存模式下运行");
    }

    return true;
}

bool Entry::enable() {
    getSelf().getLogger().debug("Enabling...");

    const auto& config = ConfigManager::getInstance().get();

    // 应用日志等级
    getSelf().getLogger().setLevel(static_cast<ll::io::LogLevel>(config.logLevel));

    // 清理过期的 IP 缓存记录
    IpDatabase::getInstance().cleanupExpired(config.cacheRetentionDays);
    if (IpDatabase::getInstance().isOpen()) {
        getSelf().getLogger().info("IP 缓存数据库就绪，当前缓存 {} 条记录", IpDatabase::getInstance().size());
    }

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
    IpDatabase::getInstance().close();
    return true;
}

bool Entry::unload() {
    getSelf().getLogger().debug("Unloading...");
    return true;
}

} // namespace IPA

LL_REGISTER_MOD(IPA::Entry, IPA::Entry::getInstance());

#include "Network/IpCache.h"

#include "Config/ConfigSerialization.h"
#include "logger.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <vector>

namespace IPA {

namespace {

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// 条目数超出上限时，淘汰最早写入的记录
void pruneOldest(std::unordered_map<std::string, CachedIpEntry>& map, size_t maxEntries) {
    if (map.size() <= maxEntries) return;
    std::vector<std::pair<std::string, int64_t>> entries;
    entries.reserve(map.size());
    for (auto const& [key, entry] : map) {
        entries.emplace_back(key, entry.timestamp);
    }
    std::sort(entries.begin(), entries.end(), [](auto const& a, auto const& b) {
        return a.second < b.second;
    });
    size_t toRemove = map.size() - maxEntries;
    for (size_t i = 0; i < toRemove; ++i) {
        map.erase(entries[i].first);
    }
}

} // namespace

IpCache& IpCache::getInstance() {
    static IpCache instance;
    return instance;
}

void IpCache::load(const std::string& path) {
    mPath = path;

    std::ifstream file(path);
    if (!file.is_open()) {
        return; // 没有缓存文件，从空缓存开始
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    try {
        nlohmann::json json = nlohmann::json::parse(content, nullptr, true, true);
        IpCacheFile    data = json.get<IpCacheFile>();

        std::lock_guard lock(mMutex);
        mPlayers = std::move(data.players);
        mIps     = std::move(data.ips);
        logger.info("IP 缓存已加载: {} 条玩家记录, {} 条 IP 记录", mPlayers.size(), mIps.size());
    } catch (const nlohmann::json::exception& e) {
        logger.warn("IP 缓存文件解析失败，将使用空缓存: {}", e.what());
    }
}

void IpCache::save() const {
    if (mPath.empty()) return;

    IpCacheFile data;
    {
        std::lock_guard lock(mMutex);
        data.players = mPlayers;
        data.ips     = mIps;
    }

    try {
        nlohmann::json json = data;
        std::ofstream  file(mPath);
        file << json.dump(4);
    } catch (const nlohmann::json::exception& e) {
        logger.warn("IP 缓存保存失败: {}", e.what());
    }
}

std::optional<IpInfo> IpCache::getForPlayer(const std::string& uuid, const std::string& ip) const {
    std::lock_guard lock(mMutex);
    auto            it = mPlayers.find(uuid);
    if (it == mPlayers.end()) return std::nullopt;
    if (it->second.ip != ip) return std::nullopt; // IP 发生变化，缓存失效
    return it->second.info;
}

std::optional<IpInfo> IpCache::getForIp(const std::string& ipOrDomain) const {
    std::lock_guard lock(mMutex);
    auto            it = mIps.find(ipOrDomain);
    if (it == mIps.end()) return std::nullopt;
    return it->second.info;
}

void IpCache::putForPlayer(const std::string& uuid, const std::string& ip, const IpInfo& info) {
    std::lock_guard lock(mMutex);
    putLocked(mPlayers, uuid, ip, info);
    pruneOldest(mPlayers, kMaxEntriesPerMap);
}

void IpCache::putForIp(const std::string& ipOrDomain, const IpInfo& info) {
    std::lock_guard lock(mMutex);
    putLocked(mIps, ipOrDomain, ipOrDomain, info);
    pruneOldest(mIps, kMaxEntriesPerMap);
}

void IpCache::putLocked(
    std::unordered_map<std::string, CachedIpEntry>& target,
    const std::string&                             key,
    const std::string&                             ip,
    const IpInfo&                                  info
) {
    CachedIpEntry entry;
    entry.ip        = ip;
    entry.info      = info;
    entry.timestamp = nowSeconds();
    target[key]     = entry;
}

size_t IpCache::size() const {
    std::lock_guard lock(mMutex);
    return mPlayers.size() + mIps.size();
}

} // namespace IPA

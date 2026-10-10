#pragma once

#include "Network/IpService.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace IPA {

// 一条缓存记录：某个 IP 对应的查询结果
struct CachedIpEntry {
    std::string ip        = "";   // 该条目对应的 IP
    IpInfo      info      = {};  // IP 查询结果
    int64_t     timestamp = 0;   // 查询时间（Unix 时间戳，秒）
};

// 缓存文件结构（plugins/IP_addr/ip_cache.json）
struct IpCacheFile {
    std::unordered_map<std::string, CachedIpEntry> players; // key: 玩家 UUID
    std::unordered_map<std::string, CachedIpEntry> ips;     // key: IP / 域名
};

// IP 查询缓存
// - 玩家维度：同一玩家 IP 未变化时直接读缓存，不请求 API
// - IP 维度：/ipinfo 命令重复查询同一 IP 时复用结果
class IpCache {
public:
    static IpCache& getInstance();

    // 从文件加载缓存（文件不存在或损坏时从空缓存开始）
    void load(const std::string& path);

    // 将缓存写入文件
    void save() const;

    // 玩家缓存：该 UUID 对应的 IP 与上次一致时返回缓存结果，否则返回 std::nullopt
    std::optional<IpInfo> getForPlayer(const std::string& uuid, const std::string& ip) const;

    // IP 缓存：返回该 IP / 域名对应的缓存结果
    std::optional<IpInfo> getForIp(const std::string& ipOrDomain) const;

    // 写入玩家缓存
    void putForPlayer(const std::string& uuid, const std::string& ip, const IpInfo& info);

    // 写入 IP 缓存
    void putForIp(const std::string& ipOrDomain, const IpInfo& info);

    // 当前缓存条目总数
    size_t size() const;

private:
    IpCache() = default;

    void putLocked(
        std::unordered_map<std::string, CachedIpEntry>& target,
        const std::string&                             key,
        const std::string&                             ip,
        const IpInfo&                                  info
    );

    mutable std::mutex                             mMutex;
    std::unordered_map<std::string, CachedIpEntry> mPlayers;
    std::unordered_map<std::string, CachedIpEntry> mIps;
    std::string                                    mPath;

    static constexpr size_t kMaxEntriesPerMap = 5000; // 单个映射的最大条目数，超出后淘汰最旧记录
};

} // namespace IPA

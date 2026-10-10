#pragma once

#include <optional>
#include <string>

namespace IPA {

// IP 查询结果
struct IpInfo {
    std::string ip;        // IP 地址
    std::string beginip;   // 起始 IP
    std::string endip;     // 结束 IP
    std::string region;    // 地理位置
    std::string isp;       // 运营商
    std::string asn;       // ASN
    std::string llc;       // 网络所有者
    double      latitude  = 0.0;
    double      longitude = 0.0;
    std::string timeZone;  // 时区（商业数据源返回）
};

// 对接 https://uapis.cn/api/v1/network/ipinfo
class IpService {
public:
    // 查询 IP / 域名信息；key 为空时使用访客免费额度
    // 失败时返回 std::nullopt，并通过 errorOut 返回错误原因
    static std::optional<IpInfo> queryIp(
        const std::string& ipOrDomain,
        const std::string& key,
        int                timeoutMs,
        std::string&       errorOut
    );
};

} // namespace IPA

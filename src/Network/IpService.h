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
    // 网络波动（超时/连接失败）、限流（429）、服务器错误（5xx）将按 retryCount/retryDelayMs 指数退避重试；
    // 确定性错误（400/401 等）不重试。全部失败时返回 std::nullopt，errorOut 为失败原因
    static std::optional<IpInfo> queryIp(
        const std::string& ipOrDomain,
        const std::string& key,
        int                timeoutMs,
        int                retryCount,
        int                retryDelayMs,
        std::string&       errorOut
    );

    // 将 API 返回的英文运营商名规范化为中文常用名（如 China Mobile ... -> 中国移动）
    // 无法匹配或已为中文时原样返回
    static std::string normalizeIsp(const std::string& raw);
};

} // namespace IPA

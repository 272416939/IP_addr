#pragma once

#include "Network/IpService.h"

#include <string>

namespace IPA {

// IP 工具：内网地址判断等
class IpUtils {
public:
    // 判断是否为内网（私有/保留）地址，支持 IPv4 与 IPv6：
    // IPv4: 0.0.0.0/8, 10.0.0.0/8, 100.64.0.0/10(CGNAT), 127.0.0.0/8,
    //       169.254.0.0/16, 172.16.0.0/12, 192.168.0.0/16
    // IPv6: ::1, fe80::/10, fc00::/7, ::ffff:x.x.x.x(递归判断内层 IPv4)
    static bool isPrivateIp(const std::string& ip);

    // 构造内网地址的查询结果（运营商显示"内网地址"，地理位置显示"内网设备"）
    static IpInfo makePrivateIpInfo(const std::string& ip);

    // 从 RakNet 的 "IP|端口" 字符串中提取纯 IP 地址（兼容 IPv4:端口、[IPv6]:端口 格式）
    static std::string extractIp(const std::string& ipAndPort);
};

} // namespace IPA

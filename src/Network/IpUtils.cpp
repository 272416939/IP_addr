#include "Network/IpUtils.h"

#include <ws2tcpip.h>

#include <cstdint>
#include <cstring>

namespace IPA {

bool IpUtils::isPrivateIp(const std::string& ip) {
    if (ip.empty()) return false;

    std::string addr = ip;

    // 去掉 IPv6 方括号，例如 "[::1]"
    if (!addr.empty() && addr.front() == '[') {
        auto close = addr.find(']');
        if (close == std::string::npos) return false;
        addr = addr.substr(1, close - 1);
    }

    // IPv4
    in_addr a4{};
    if (inet_pton(AF_INET, addr.c_str(), &a4) == 1) {
        uint32_t h = ntohl(a4.s_addr);
        return (h >> 24) == 0x00                  // 0.0.0.0/8       本网络
            || (h >> 24) == 0x0A                  // 10.0.0.0/8      私有
            || (h >> 24) == 0x7F                  // 127.0.0.0/8     环回
            || (h >> 20) == 0x0AC1                // 172.16.0.0/12   私有
            || (h >> 16) == 0xA9FE                // 169.254.0.0/16  链路本地
            || (h >> 16) == 0xC0A8                // 192.168.0.0/16  私有
            || (h >> 22) == 0x191;                // 100.64.0.0/10  CGNAT (100.64.0.0 ~ 100.127.255.255)
    }

    // IPv6
    in6_addr a6{};
    if (inet_pton(AF_INET6, addr.c_str(), &a6) == 1) {
        static const unsigned char kLoopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        if (std::memcmp(&a6, kLoopback, 16) == 0) return true;      // ::1
        if (a6.s6_addr[0] == 0xFE && (a6.s6_addr[1] & 0xC0) == 0x80) return true; // fe80::/10 链路本地
        if ((a6.s6_addr[0] & 0xFE) == 0xFC) return true;           // fc00::/7 唯一本地地址

        // IPv4-mapped IPv6（::ffff:x.x.x.x）：递归判断内层 IPv4
        static const unsigned char kV4MappedPrefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
        if (std::memcmp(&a6, kV4MappedPrefix, 12) == 0) {
            char v4[INET_ADDRSTRLEN] = {};
            if (inet_ntop(AF_INET, &a6.s6_addr[12], v4, sizeof(v4))) {
                return isPrivateIp(v4);
            }
        }
    }

    return false;
}

IpInfo IpUtils::makePrivateIpInfo(const std::string& ip) {
    IpInfo info;
    info.ip     = ip;
    info.isp    = "内网地址";
    info.region = "内网设备";
    return info;
}

std::string IpUtils::extractIp(const std::string& ipAndPort) {
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

} // namespace IPA

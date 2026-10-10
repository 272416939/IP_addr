#include "Network/IpService.h"

#include "Network/HttpClient.h"

#include <nlohmann/json.hpp>

#include <cctype>

namespace IPA {

namespace {

// 对查询参数进行 URL 编码
std::string urlEncode(std::string const& value) {
    std::string result;
    result.reserve(value.size() * 3);
    static char const* hex = "0123456789ABCDEF";
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result += (char)c;
        } else {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 0x0F];
        }
    }
    return result;
}

// 去掉首尾空白与引号，防止用户手滑把 key 写成了 "xxx"
std::string cleanKey(std::string value) {
    while (!value.empty() && (value.front() == '"' || value.front() == '\'' || std::isspace((unsigned char)value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == '"' || value.back() == '\'' || std::isspace((unsigned char)value.back()))) {
        value.pop_back();
    }
    return value;
}

} // namespace

std::optional<IpInfo> IpService::queryIp(
    const std::string& ipOrDomain,
    const std::string& key,
    int                timeoutMs,
    std::string&       errorOut
) {
    errorOut.clear();

    std::string cleanedKey = cleanKey(key);

    // 请求地址：https://uapis.cn/api/v1/network/ipinfo?ip=<ip或域名>
    std::string url = "https://uapis.cn/api/v1/network/ipinfo?ip=" + urlEncode(ipOrDomain);

    // 配置了 key 时通过 Authorization 请求头传递；未配置时使用访客免费额度
    std::map<std::string, std::string> headers;
    if (!cleanedKey.empty()) {
        headers["Authorization"] = "Bearer " + cleanedKey;
    }

    HttpResponse response = HttpClient::get(url, headers, timeoutMs);
    if (!response.success) {
        errorOut = "网络请求失败: " + response.error;
        return std::nullopt;
    }

    nlohmann::json json;
    try {
        json = nlohmann::json::parse(response.body);
    } catch (const nlohmann::json::parse_error&) {
        errorOut = "响应解析失败 (HTTP " + std::to_string(response.statusCode) + ")";
        return std::nullopt;
    }

    // key 无效或已失效
    if (response.statusCode == 401 || json.value("code", "") == "INVALID_API_KEY") {
        errorOut = "API Key 无效或已失效，请检查 config.json 中的 key 配置";
        return std::nullopt;
    }

    if (response.statusCode != 200) {
        errorOut = "查询失败 (HTTP " + std::to_string(response.statusCode) + "): "
                 + json.value("message", "未知错误");
        return std::nullopt;
    }

    if (!json.contains("ip")) {
        errorOut = json.value("message", "未查询到该 IP 的信息");
        return std::nullopt;
    }

    IpInfo info;
    info.ip       = json.value("ip", "");
    info.beginip  = json.value("beginip", "");
    info.endip    = json.value("endip", "");
    info.region   = json.value("region", "");
    info.isp      = json.value("isp", "");
    info.asn      = json.value("asn", "");
    info.llc      = json.value("llc", "");
    info.latitude  = json.value("latitude", 0.0);
    info.longitude = json.value("longitude", 0.0);
    info.timeZone = json.value("time_zone", "");
    return info;
}

} // namespace IPA

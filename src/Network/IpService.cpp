#include "Network/IpService.h"

#include "Network/HttpClient.h"
#include "logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <thread>

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

// 类型安全地从 JSON 对象读取字符串字段（字段缺失或类型不符时返回默认值）
std::string safeGetString(nlohmann::json const& j, std::string const& key, std::string const& def = "") {
    if (j.contains(key) && j[key].is_string()) {
        return j[key].get<std::string>();
    }
    return def;
}

// 类型安全地从 JSON 对象读取数值字段
double safeGetNumber(nlohmann::json const& j, std::string const& key, double def = 0.0) {
    if (j.contains(key) && j[key].is_number()) {
        return j[key].get<double>();
    }
    return def;
}

// 子串匹配（用于多词短语）
bool containsPhrase(std::string const& haystack, std::string const& needle) {
    return haystack.find(needle) != std::string::npos;
}

// 按词边界匹配（用于短 key，避免 "aws" 命中 "laws" 之类的情况）
bool hasToken(std::string const& haystack, std::string const& token) {
    if (token.empty()) return false;
    size_t pos = 0;
    while ((pos = haystack.find(token, pos)) != std::string::npos) {
        bool leftOk  = pos == 0 || !std::isalnum((unsigned char)haystack[pos - 1]);
        bool rightOk = pos + token.size() >= haystack.size()
                    || !std::isalnum((unsigned char)haystack[pos + token.size()]);
        if (leftOk && rightOk) return true;
        pos += token.size();
    }
    return false;
}

} // namespace

std::string IpService::normalizeIsp(const std::string& raw) {
    if (raw.empty()) return raw;

    // 已包含非 ASCII 字符（API 有时直接返回中文，如"阿里云"）时保持原样
    for (unsigned char c : raw) {
        if (c >= 0x80) return raw;
    }

    std::string lower = raw;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });

    auto anyPhrase = [&](std::initializer_list<const char*> keys) {
        return std::any_of(keys.begin(), keys.end(), [&](const char* k) {
            return containsPhrase(lower, k);
        });
    };
    auto anyToken = [&](std::initializer_list<const char*> keys) {
        return std::any_of(keys.begin(), keys.end(), [&](const char* k) {
            return hasToken(lower, k);
        });
    };

    // 三大运营商（注意 API 存在 "China Mobile Communicaitons" 等拼写变体，用 "china mobile" 匹配）
    if (anyPhrase({"china mobile", "cmcc"}))               return "中国移动";
    if (anyPhrase({"chinanet", "china telecom", "chinatelecom"})) return "中国电信";
    if (anyPhrase({"china unicom", "unicom", "china169"})) return "中国联通";
    if (anyPhrase({"china broadcast", "china broadcasting"}) || anyToken({"cbn"})) return "中国广电";
    if (anyPhrase({"cernet", "education and research"}))   return "教育网";
    if (anyPhrase({"dr.peng", "dr peng"}))                 return "鹏博士";

    // 云服务商
    if (anyPhrase({"alibaba", "aliyun", "alicloud"}))      return "阿里云";
    if (anyPhrase({"tencent"}))                            return "腾讯云";
    if (anyPhrase({"huawei"}))                             return "华为云";
    if (anyPhrase({"baidu"}))                              return "百度云";
    if (anyPhrase({"bytedance", "volcengine"}))            return "火山引擎";
    if (anyPhrase({"kingsoft", "ksyun"}))                  return "金山云";
    if (anyPhrase({"jingdong", "jdcloud"}))                return "京东云";
    if (anyPhrase({"ucloud"}))                             return "优刻得";
    if (anyPhrase({"qingcloud"}))                          return "青云";
    if (anyPhrase({"amazon"}) || anyToken({"aws"}))        return "亚马逊 AWS";
    if (anyPhrase({"google cloud"}) || anyToken({"gcp"}))  return "谷歌云";
    if (anyPhrase({"microsoft", "azure"}))                 return "微软 Azure";
    if (anyPhrase({"oracle"}))                             return "甲骨文云";

    // 海外 IDC / CDN
    if (anyPhrase({"zenlayer"}))                           return "Zenlayer";
    if (anyPhrase({"cloudflare"}))                         return "Cloudflare";
    if (anyPhrase({"digitalocean"}))                       return "DigitalOcean";
    if (anyPhrase({"linode", "akamai"}))                   return "Linode";
    if (anyPhrase({"vultr"}))                              return "Vultr";
    if (anyPhrase({"hetzner"}))                            return "Hetzner";
    if (anyPhrase({"ovh"}))                                return "OVH";
    if (anyPhrase({"contabo"}))                            return "Contabo";

    return raw; // 无法识别，原样返回
}

namespace {

// 单次尝试的结果分类
enum class AttemptResult {
    Success,  // 查询成功（结果在 infoOut 中）
    Retryable, // 可重试的失败（网络波动 / 限流 / 服务器错误），原因在 errorOut 中
    Fatal,    // 确定性失败，重试无意义（参数错误 / key 无效 / 未收录），原因在 errorOut 中
};

AttemptResult processResponse(const HttpResponse& response, IpInfo& infoOut, std::string& errorOut) {
    // 网络层失败（超时、连接失败等）——网络波动，可重试
    if (!response.success) {
        errorOut = "网络请求失败: " + response.error;
        return AttemptResult::Retryable;
    }

    nlohmann::json json;
    try {
        json = nlohmann::json::parse(response.body);
    } catch (const nlohmann::json::exception&) { // 包括 parse_error / type_error 等
        // 200 响应体无法解析属于异常情况，按可重试处理
        errorOut = "响应解析失败 (HTTP " + std::to_string(response.statusCode) + ")";
        return AttemptResult::Retryable;
    }

    // 访客限流: {"code":429,"limit":4,"message":"Visitor rate limit exceeded"}——等待后可重试
    if (response.statusCode == 429) {
        errorOut = "API 请求过于频繁（访客限额 4 次/秒）";
        return AttemptResult::Retryable;
    }

    // API 服务器内部错误——可重试
    if (response.statusCode >= 500) {
        errorOut = "API 服务器错误 (HTTP " + std::to_string(response.statusCode) + "): "
                 + safeGetString(json, "message", "未知错误");
        return AttemptResult::Retryable;
    }

    // key 无效或已失效（注意：错误响应中的 code 字段可能是数字，需类型安全读取）——重试无意义
    if (response.statusCode == 401 || safeGetString(json, "code") == "INVALID_API_KEY") {
        errorOut = "API Key 无效或已失效，请检查 config.json 中的 key 配置";
        return AttemptResult::Fatal;
    }

    // 其他非 200（如 400 参数错误、404 未收录）——重试无意义
    if (response.statusCode != 200) {
        errorOut = "查询失败 (HTTP " + std::to_string(response.statusCode) + "): "
                 + safeGetString(json, "message", "未知错误");
        return AttemptResult::Fatal;
    }

    if (!json.contains("ip")) {
        errorOut = safeGetString(json, "message", "未查询到该 IP 的信息");
        return AttemptResult::Fatal;
    }

    IpInfo info;
    info.ip       = safeGetString(json, "ip");
    info.beginip  = safeGetString(json, "beginip");
    info.endip    = safeGetString(json, "endip");
    info.region   = safeGetString(json, "region");
    info.asn      = safeGetString(json, "asn");
    info.latitude  = safeGetNumber(json, "latitude");
    info.longitude = safeGetNumber(json, "longitude");
    info.timeZone = safeGetString(json, "time_zone");

    // 运营商名称规范化为中文常用名；isp 为空时回退到 llc（网络所有者）字段
    info.isp = IpService::normalizeIsp(safeGetString(json, "isp"));
    if (info.isp.empty()) {
        info.isp = IpService::normalizeIsp(safeGetString(json, "llc"));
    }
    info.llc = safeGetString(json, "llc");

    infoOut  = std::move(info);
    errorOut.clear();
    return AttemptResult::Success;
}

} // namespace

std::optional<IpInfo> IpService::queryIp(
    const std::string& ipOrDomain,
    const std::string& key,
    int                timeoutMs,
    int                retryCount,
    int                retryDelayMs,
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

    // 重试机制：应对网络波动造成的超时/失败
    const int maxRetries = retryCount > 0 ? retryCount : 0;
    int       attempt    = 0; // 已失败并即将重试的次数

    while (true) {
        HttpResponse response = HttpClient::get(url, headers, timeoutMs);

        IpInfo      info;
        std::string reason;
        AttemptResult result = processResponse(response, info, reason);

        if (result == AttemptResult::Success) {
            return info;
        }

        if (result == AttemptResult::Fatal || attempt >= maxRetries) {
            if (attempt > 0) {
                errorOut = "重试 " + std::to_string(attempt) + " 次后仍失败: " + reason;
            } else {
                errorOut = reason;
            }
            return std::nullopt;
        }

        // 指数退避：base, 2*base, 4*base ... 封顶 4 倍基础延迟
        int delayMs = retryDelayMs > 0 ? retryDelayMs : 500;
        for (int i = 0; i < attempt; ++i) {
            delayMs = std::min(delayMs * 2, (retryDelayMs > 0 ? retryDelayMs : 500) * 4);
        }

        logger.info(
            "IP {} 查询失败（{}），{} 毫秒后进行第 {}/{} 次重试",
            ipOrDomain,
            reason,
            delayMs,
            attempt + 1,
            maxRetries
        );
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        ++attempt;
    }
}

} // namespace IPA

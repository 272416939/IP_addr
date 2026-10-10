#pragma once

#include <map>
#include <string>

namespace IPA {

// 一次 HTTP 请求的结果
struct HttpResponse {
    bool        success    = false; // 请求是否完成（收到响应，不代表业务成功）
    int         statusCode = 0;     // HTTP 状态码
    std::string body;               // 响应体
    std::string error;              // 失败原因（success 为 false 时有效）
};

// 基于 WinHTTP 的简易同步 HTTP 客户端（仅支持 GET）
class HttpClient {
public:
    static HttpResponse get(
        const std::string&                        url,
        const std::map<std::string, std::string>& headers = {},
        int                                       timeoutMs = 5000
    );
};

} // namespace IPA

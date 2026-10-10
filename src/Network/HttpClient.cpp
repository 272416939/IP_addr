#include "Network/HttpClient.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <vector>

namespace IPA {

namespace {

std::wstring utf8ToWide(std::string const& str) {
    if (str.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring result((size_t)size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), result.data(), size);
    return result;
}

// 解析 URL，取出 scheme / host / port / path
bool parseUrl(std::string const& url, bool& useHttps, std::string& host, int& port, std::string& path) {
    std::string rest = url;
    useHttps        = true;

    auto schemePos = url.find("://");
    if (schemePos != std::string::npos) {
        std::string scheme = url.substr(0, schemePos);
        useHttps           = (scheme != "http");
        rest               = url.substr(schemePos + 3);
    }

    auto slash = rest.find('/');
    std::string authority = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    path                  = (slash == std::string::npos) ? "/" : rest.substr(slash);

    // 去掉用户信息 user:pass@
    auto at = authority.find('@');
    if (at != std::string::npos) authority = authority.substr(at + 1);
    if (authority.empty()) return false;

    port = useHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;

    if (authority.front() == '[') {
        // IPv6 字面量，形如 [::1]:443
        auto close = authority.find(']');
        if (close == std::string::npos) return false;
        host = authority.substr(1, close - 1);
        if (close + 2 < authority.size() && authority[close + 1] == ':') {
            try {
                port = std::stoi(authority.substr(close + 2));
            } catch (...) {
                return false;
            }
        }
    } else {
        auto colon = authority.rfind(':');
        if (colon != std::string::npos) {
            host = authority.substr(0, colon);
            try {
                port = std::stoi(authority.substr(colon + 1));
            } catch (...) {
                return false;
            }
        } else {
            host = authority;
        }
    }
    return !host.empty();
}

} // namespace

HttpResponse HttpClient::get(
    const std::string&                        url,
    const std::map<std::string, std::string>& headers,
    int                                       timeoutMs
) {
    HttpResponse response;

    bool        useHttps = true;
    std::string host;
    int         port = 0;
    std::string path;
    if (!parseUrl(url, useHttps, host, port, path)) {
        response.error = "Invalid URL: " + url;
        return response;
    }

    HINTERNET hSession = WinHttpOpen(
        L"IP_addr",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) {
        response.error = "WinHttpOpen failed, error " + std::to_string(GetLastError());
        return response;
    }

    // 解析、连接、发送、接收四个阶段的超时，单位毫秒
    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET hConnect = WinHttpConnect(hSession, utf8ToWide(host).c_str(), (INTERNET_PORT)port, 0);
    if (!hConnect) {
        response.error = "WinHttpConnect failed, error " + std::to_string(GetLastError());
        WinHttpCloseHandle(hSession);
        return response;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        utf8ToWide(path).c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        useHttps ? WINHTTP_FLAG_SECURE : 0
    );
    if (!hRequest) {
        response.error = "WinHttpOpenRequest failed, error " + std::to_string(GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }

    for (auto const& [name, value] : headers) {
        std::wstring headerLine = utf8ToWide(name + ": " + value);
        WinHttpAddRequestHeaders(hRequest, headerLine.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    }

    if (!WinHttpSendRequest(
            hRequest,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0
        )) {
        response.error = "WinHttpSendRequest failed, error " + std::to_string(GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        response.error = "WinHttpReceiveResponse failed, error " + std::to_string(GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }

    // 查询状态码
    DWORD statusCode = 0;
    DWORD headerSize = sizeof(statusCode);
    if (WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &headerSize,
            WINHTTP_NO_HEADER_INDEX
        )) {
        response.statusCode = (int)statusCode;
    }

    // 读取响应体
    std::string body;
    DWORD       bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable);
        DWORD             bytesRead = 0;
        if (!WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead) || bytesRead == 0) {
            break;
        }
        body.append(buffer.data(), bytesRead);
    }
    response.body = std::move(body);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    response.success = true; // 请求已完成
    return response;
}

} // namespace IPA

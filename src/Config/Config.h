#pragma once

#include <string>

// IP_addr 插件配置（聚合类型，由 ConfigSerialization.h 中的通用序列化器处理）
struct Config {
    std::string key             = "";    // uapis.cn API Key，留空则使用访客免费额度
    bool        enableJoinQuery = true; // 玩家加入时是否查询并输出 IP 信息
    int         timeoutMs       = 5000;  // HTTP 请求超时时间（毫秒）
    int         logLevel        = 3;     // 日志等级：-1 Off, 0 Fatal, 1 Error, 2 Warn, 3 Info, 4 Debug, 5 Trace
};


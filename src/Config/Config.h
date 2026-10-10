#pragma once

#include <string>

// IP_addr 插件配置（聚合类型，由 ConfigSerialization.h 中的通用序列化器处理）
struct Config {
    std::string key             = "";    // uapis.cn API Key，留空则使用访客免费额度
    bool        enableJoinQuery = true; // 玩家加入时是否查询并输出 IP 信息
    int         timeoutMs       = 5000;  // 单次 HTTP 请求超时时间（毫秒）
    int         logLevel        = 3;     // 日志等级：-1 Off, 0 Fatal, 1 Error, 2 Warn, 3 Info, 4 Debug, 5 Trace

    // SQLite 缓存数据库路径（相对于服务器根目录）
    std::string databasePath      = "plugins/IP_addr/data/ip_cache.db";
    // IP 缓存保留天数：超过该天数未更新的记录在启动时自动清理；<= 0 表示不清理
    int         cacheRetentionDays = 90;

    // 查询失败时的重试次数（不含首次请求），应对网络波动；0 表示不重试
    int retryCount   = 5;
    // 重试基础延迟（毫秒），采用指数退避：base、2×base、4×base ... 封顶 4×base
    int retryDelayMs = 500;
};



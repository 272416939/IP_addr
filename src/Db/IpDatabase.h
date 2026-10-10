#pragma once

#include "Network/IpService.h"

#include <sqlite3.h>

#include <mutex>
#include <optional>
#include <string>

namespace IPA {

// IP 查询结果缓存（SQLite 存储，默认位于 plugins/IP_addr/data/ip_cache.db）
//
// 表结构：
//   ip_cache  —— IP/域名 -> 查询结果（多个玩家/命令可共享同一条记录，不重复存储）
//   player_ip —— 玩家 UUID -> 上次的 IP（用于判断玩家 IP 是否发生变化）
//
// 优化点：
//   - WAL 日志模式 + synchronous=NORMAL：读写互不阻塞，掉电仅可能丢失最后一次写入
//   - busy_timeout=5s：避免 "database is locked" 报错
//   - 全部热点 SQL 预编译并复用，避免重复解析
//   - 玩家信息与 IP 信息在同一事务中写入，保证两表数据一致
//   - updated_at 索引 + 启动时自动清理过期记录，防止数据库无限增长
class IpDatabase {
public:
    static IpDatabase& getInstance();

    ~IpDatabase();

    IpDatabase(const IpDatabase&)            = delete;
    IpDatabase& operator=(const IpDatabase&) = delete;

    // 打开（不存在则创建）数据库并初始化表结构；失败返回 false
    bool open(const std::string& dbPath);

    // 关闭数据库
    void close();

    // 数据库是否已打开
    bool isOpen() const;

    // 查询某个 IP / 域名的缓存结果
    std::optional<IpInfo> getIpInfo(const std::string& ipOrDomain) const;

    // 查询玩家缓存：该 UUID 对应的 IP 与传入 IP 一致时返回其详情，否则返回 std::nullopt
    std::optional<IpInfo> getCachedForPlayer(const std::string& uuid, const std::string& ip) const;

    // 保存 IP 查询结果到 ip_cache 表（/ipinfo 命令等无玩家上下文的场景）
    void saveIpInfo(const std::string& ipOrDomain, const IpInfo& info);

    // 保存玩家查询结果：事务内同时更新 ip_cache 与 player_ip 两表
    void savePlayerResult(const std::string& uuid, const std::string& ip, const IpInfo& info);

    // 清理早于 retentionDays 天未更新的记录，返回删除的 ip_cache 条数；retentionDays <= 0 时不清理
    int cleanupExpired(int retentionDays);

    // ip_cache 表的记录数
    size_t size() const;

private:
    IpDatabase() = default;

    // RAII 事务：构造时 BEGIN IMMEDIATE，析构时未提交则自动 ROLLBACK
    class Transaction {
    public:
        explicit Transaction(sqlite3* db);
        ~Transaction();

        void commit();

        Transaction(const Transaction&)            = delete;
        Transaction& operator=(const Transaction&) = delete;

    private:
        sqlite3* mDb;
        bool     mActive = false;
    };

    bool prepareStatements();
    void finalizeStatements();
    bool exec(const char* sql) const;

    // 从查询结果行填充 IpInfo（列顺序见预编译的 SELECT 语句）
    static IpInfo rowToInfo(sqlite3_stmt* stmt);

    sqlite3*     mDb = nullptr;
    mutable std::mutex mMutex;

    // 预编译语句（open 时准备，close 时释放）
    sqlite3_stmt* mStmtInsertIpCache   = nullptr;
    sqlite3_stmt* mStmtInsertPlayerIp  = nullptr;
    sqlite3_stmt* mStmtSelectIpInfo    = nullptr;
    sqlite3_stmt* mStmtSelectForPlayer = nullptr;
    sqlite3_stmt* mStmtCleanup         = nullptr;
    sqlite3_stmt* mStmtCount           = nullptr;
};

} // namespace IPA

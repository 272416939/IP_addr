#include "Db/IpDatabase.h"

#include "logger.h"

#include <chrono>

namespace IPA {

namespace {

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

void bindText(sqlite3_stmt* stmt, int index, const std::string& value) {
    sqlite3_bind_text(stmt, index, value.c_str(), (int)value.size(), SQLITE_TRANSIENT);
}

void bindInt64(sqlite3_stmt* stmt, int index, int64_t value) {
    sqlite3_bind_int64(stmt, index, value);
}

void bindDouble(sqlite3_stmt* stmt, int index, double value) {
    sqlite3_bind_double(stmt, index, value);
}

std::string colText(sqlite3_stmt* stmt, int index) {
    const unsigned char* text = sqlite3_column_text(stmt, index);
    return text ? std::string(reinterpret_cast<const char*>(text)) : std::string();
}

double colDouble(sqlite3_stmt* stmt, int index) {
    return sqlite3_column_double(stmt, index);
}

} // namespace

IpDatabase& IpDatabase::getInstance() {
    static IpDatabase instance;
    return instance;
}

IpDatabase::~IpDatabase() {
    close();
}

bool IpDatabase::open(const std::string& dbPath) {
    std::lock_guard lock(mMutex);
    if (mDb) return true; // 已打开

    if (sqlite3_open_v2(
            dbPath.c_str(),
            &mDb,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
            nullptr
        )
        != SQLITE_OK) {
        logger.error("无法打开数据库 {}: {}", dbPath, mDb ? sqlite3_errmsg(mDb) : "未知错误");
        if (mDb) {
            sqlite3_close_v2(mDb);
            mDb = nullptr;
        }
        return false;
    }

    // 性能与并发优化
    exec("PRAGMA journal_mode=WAL;");   // WAL：读者不阻塞写者，写者不阻塞读者
    exec("PRAGMA synchronous=NORMAL;"); // WAL 模式下 NORMAL 兼顾性能与崩溃安全
    exec("PRAGMA busy_timeout=5000;");  // 锁等待 5 秒，避免 database is locked
    exec("PRAGMA temp_store=MEMORY;");  // 临时表/索引存内存

    // 初始化表结构
    const char* ddl = R"DDL(
        CREATE TABLE IF NOT EXISTS ip_cache (
            ip         TEXT PRIMARY KEY,
            region     TEXT,
            isp        TEXT,
            asn        TEXT,
            llc        TEXT,
            beginip    TEXT,
            endip      TEXT,
            latitude   REAL,
            longitude  REAL,
            time_zone  TEXT,
            updated_at INTEGER NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_ip_cache_updated_at ON ip_cache(updated_at);
        CREATE TABLE IF NOT EXISTS player_ip (
            uuid       TEXT PRIMARY KEY,
            ip         TEXT NOT NULL,
            updated_at INTEGER NOT NULL
        );
    )DDL";

    char* errMsg = nullptr;
    if (sqlite3_exec(mDb, ddl, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        logger.error("创建数据表失败: {}", errMsg ? errMsg : "未知错误");
        sqlite3_free(errMsg);
        close();
        return false;
    }

    if (!prepareStatements()) {
        close();
        return false;
    }

    logger.debug("IP 缓存数据库已打开: {}", dbPath);
    return true;
}

void IpDatabase::close() {
    std::lock_guard lock(mMutex);
    finalizeStatements();
    if (mDb) {
        sqlite3_close_v2(mDb);
        mDb = nullptr;
    }
}

bool IpDatabase::isOpen() const {
    std::lock_guard lock(mMutex);
    return mDb != nullptr;
}

std::optional<IpInfo> IpDatabase::getIpInfo(const std::string& ipOrDomain) const {
    std::lock_guard lock(mMutex);
    if (!mDb) return std::nullopt;

    sqlite3_reset(mStmtSelectIpInfo);
    bindText(mStmtSelectIpInfo, 1, ipOrDomain);

    std::optional<IpInfo> result;
    if (sqlite3_step(mStmtSelectIpInfo) == SQLITE_ROW) {
        result = rowToInfo(mStmtSelectIpInfo);
        result->ip = ipOrDomain; // ip 不在 SELECT 列中，用查询键回填
    }
    sqlite3_reset(mStmtSelectIpInfo);
    return result;
}

std::optional<IpInfo> IpDatabase::getCachedForPlayer(const std::string& uuid, const std::string& ip) const {
    std::lock_guard lock(mMutex);
    if (!mDb) return std::nullopt;

    sqlite3_reset(mStmtSelectForPlayer);
    bindText(mStmtSelectForPlayer, 1, uuid);
    bindText(mStmtSelectForPlayer, 2, ip);

    std::optional<IpInfo> result;
    // 一条 JOIN 同时验证「玩家 IP 未变化」并取出 IP 详情
    if (sqlite3_step(mStmtSelectForPlayer) == SQLITE_ROW) {
        result = rowToInfo(mStmtSelectForPlayer);
        result->ip = ip;
    }
    sqlite3_reset(mStmtSelectForPlayer);
    return result;
}
void IpDatabase::saveIpInfo(const std::string& ipOrDomain, const IpInfo& info) {
    std::lock_guard lock(mMutex);
    if (!mDb) return;

    Transaction tx(mDb); // 析构时未提交则自动 ROLLBACK

    sqlite3_reset(mStmtInsertIpCache);
    bindText(mStmtInsertIpCache, 1, ipOrDomain);
    bindText(mStmtInsertIpCache, 2, info.region);
    bindText(mStmtInsertIpCache, 3, info.isp);
    bindText(mStmtInsertIpCache, 4, info.asn);
    bindText(mStmtInsertIpCache, 5, info.llc);
    bindText(mStmtInsertIpCache, 6, info.beginip);
    bindText(mStmtInsertIpCache, 7, info.endip);
    bindDouble(mStmtInsertIpCache, 8, info.latitude);
    bindDouble(mStmtInsertIpCache, 9, info.longitude);
    bindText(mStmtInsertIpCache, 10, info.timeZone);
    bindInt64(mStmtInsertIpCache, 11, nowSeconds());

    if (sqlite3_step(mStmtInsertIpCache) != SQLITE_DONE) {
        logger.warn("写入 ip_cache 失败: {}", sqlite3_errmsg(mDb));
        sqlite3_reset(mStmtInsertIpCache);
        return; // 事务析构时回滚
    }
    sqlite3_reset(mStmtInsertIpCache);

    tx.commit();
}

void IpDatabase::savePlayerResult(const std::string& uuid, const std::string& ip, const IpInfo& info) {
    std::lock_guard lock(mMutex);
    if (!mDb) return;

    Transaction tx(mDb); // 两表在同一事务中写入，保证数据一致

    // 1. 写入 IP 详情（INSERT OR REPLACE，多个玩家同一 IP 时只保留一条记录）
    sqlite3_reset(mStmtInsertIpCache);
    bindText(mStmtInsertIpCache, 1, ip);
    bindText(mStmtInsertIpCache, 2, info.region);
    bindText(mStmtInsertIpCache, 3, info.isp);
    bindText(mStmtInsertIpCache, 4, info.asn);
    bindText(mStmtInsertIpCache, 5, info.llc);
    bindText(mStmtInsertIpCache, 6, info.beginip);
    bindText(mStmtInsertIpCache, 7, info.endip);
    bindDouble(mStmtInsertIpCache, 8, info.latitude);
    bindDouble(mStmtInsertIpCache, 9, info.longitude);
    bindText(mStmtInsertIpCache, 10, info.timeZone);
    bindInt64(mStmtInsertIpCache, 11, nowSeconds());

    if (sqlite3_step(mStmtInsertIpCache) != SQLITE_DONE) {
        logger.warn("写入 ip_cache 失败: {}", sqlite3_errmsg(mDb));
        sqlite3_reset(mStmtInsertIpCache);
        return;
    }
    sqlite3_reset(mStmtInsertIpCache);

    // 2. 写入玩家 -> IP 映射
    sqlite3_reset(mStmtInsertPlayerIp);
    bindText(mStmtInsertPlayerIp, 1, uuid);
    bindText(mStmtInsertPlayerIp, 2, ip);
    bindInt64(mStmtInsertPlayerIp, 3, nowSeconds());

    if (sqlite3_step(mStmtInsertPlayerIp) != SQLITE_DONE) {
        logger.warn("写入 player_ip 失败: {}", sqlite3_errmsg(mDb));
        sqlite3_reset(mStmtInsertPlayerIp);
        return;
    }
    sqlite3_reset(mStmtInsertPlayerIp);

    tx.commit();
}

int IpDatabase::cleanupExpired(int retentionDays) {
    std::lock_guard lock(mMutex);
    if (!mDb || retentionDays <= 0) return 0;

    int64_t cutoff = nowSeconds() - (int64_t)retentionDays * 86400;

    sqlite3_reset(mStmtCleanup);
    bindInt64(mStmtCleanup, 1, cutoff);

    int changes = 0;
    if (sqlite3_step(mStmtCleanup) == SQLITE_DONE) {
        changes = sqlite3_changes(mDb);
    }
    sqlite3_reset(mStmtCleanup);

    if (changes > 0) {
        logger.info("已清理 {} 条过期 IP 缓存（超过 {} 天未更新）", changes, retentionDays);
    }
    return changes;
}

size_t IpDatabase::size() const {
    std::lock_guard lock(mMutex);
    if (!mDb) return 0;

    sqlite3_reset(mStmtCount);
    size_t count = 0;
    if (sqlite3_step(mStmtCount) == SQLITE_ROW) {
        count = (size_t)sqlite3_column_int64(mStmtCount, 0);
    }
    sqlite3_reset(mStmtCount);
    return count;
}

bool IpDatabase::prepareStatements() {
    struct StatementDef {
        const char*    sql;
        sqlite3_stmt** target;
    };
    static const StatementDef defs[] = {
        {"INSERT OR REPLACE INTO ip_cache(ip,region,isp,asn,llc,beginip,endip,latitude,longitude,time_zone,updated_at)"
         " VALUES(?,?,?,?,?,?,?,?,?,?,?)",
         &mStmtInsertIpCache},
        {"INSERT OR REPLACE INTO player_ip(uuid,ip,updated_at) VALUES(?,?,?)",
         &mStmtInsertPlayerIp},
        {"SELECT region,isp,asn,llc,beginip,endip,latitude,longitude,time_zone FROM ip_cache WHERE ip=?",
         &mStmtSelectIpInfo},
        {"SELECT c.region,c.isp,c.asn,c.llc,c.beginip,c.endip,c.latitude,c.longitude,c.time_zone"
         " FROM player_ip p JOIN ip_cache c ON p.ip = c.ip WHERE p.uuid = ? AND p.ip = ?",
         &mStmtSelectForPlayer},
        {"DELETE FROM ip_cache WHERE updated_at < ?",
         &mStmtCleanup},
        {"SELECT COUNT(*) FROM ip_cache",
         &mStmtCount},
    };

    for (const auto& def : defs) {
        if (sqlite3_prepare_v2(mDb, def.sql, -1, def.target, nullptr) != SQLITE_OK) {
            logger.error("预处理 SQL 失败: {} ({})", sqlite3_errmsg(mDb), def.sql);
            return false;
        }
    }
    return true;
}

void IpDatabase::finalizeStatements() {
    for (sqlite3_stmt** stmt : {&mStmtInsertIpCache,
                                &mStmtInsertPlayerIp,
                                &mStmtSelectIpInfo,
                                &mStmtSelectForPlayer,
                                &mStmtCleanup,
                                &mStmtCount}) {
        if (*stmt) {
            sqlite3_finalize(*stmt);
            *stmt = nullptr;
        }
    }
}

bool IpDatabase::exec(const char* sql) const {
    char* errMsg = nullptr;
    if (sqlite3_exec(mDb, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        logger.warn("执行 SQL 失败: {} ({})", errMsg ? errMsg : "未知错误", sql);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

IpInfo IpDatabase::rowToInfo(sqlite3_stmt* stmt) {
    IpInfo info;
    info.region    = colText(stmt, 0);
    info.isp       = colText(stmt, 1);
    info.asn       = colText(stmt, 2);
    info.llc       = colText(stmt, 3);
    info.beginip   = colText(stmt, 4);
    info.endip     = colText(stmt, 5);
    info.latitude  = colDouble(stmt, 6);
    info.longitude = colDouble(stmt, 7);
    info.timeZone  = colText(stmt, 8);
    return info;
}

IpDatabase::Transaction::Transaction(sqlite3* db) : mDb(db) {
    char* errMsg = nullptr;
    if (sqlite3_exec(mDb, "BEGIN IMMEDIATE", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        logger.warn("开启事务失败: {}", errMsg ? errMsg : "未知错误");
        sqlite3_free(errMsg);
        mActive = false;
    } else {
        mActive = true;
    }
}

IpDatabase::Transaction::~Transaction() {
    if (mActive) {
        sqlite3_exec(mDb, "ROLLBACK", nullptr, nullptr, nullptr); // 未提交则自动回滚
    }
}

void IpDatabase::Transaction::commit() {
    if (!mActive) return;
    char* errMsg = nullptr;
    if (sqlite3_exec(mDb, "COMMIT", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        logger.error("提交事务失败: {}", errMsg ? errMsg : "未知错误");
        sqlite3_free(errMsg);
        sqlite3_exec(mDb, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    mActive = false;
}

} // namespace IPA


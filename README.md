# IP_addr

LeviLamina 26.20.7 原生插件：玩家加入服务器时，在控制台输出玩家的 **IP 地址、运营商、地理位置**。

IP 信息来自 [uapis.cn](https://uapis.cn/docs/api-reference/get-network-ipinfo) 的 IP 查询 API。

## 功能

- 玩家加入服务器（登录）时，自动查询并在控制台输出：

  ```
  玩家 Allen 加入游戏 IP: 211.136.192.6 运营商: 中国移动 地理位置: 中国 广东 广州
  ```

- **IP 缓存优化**：SQLite 数据库存储（`plugins/IP_addr/data/ip_cache.db`）。同一玩家 IP 未变化时直接读取数据库缓存，不请求 API；只有 IP 变化时才重新查询，节省 API 额度。
- **内网地址免查询**：玩家使用内网 IP（如 `192.168.x.x`、`10.x.x.x`、`172.16-31.x.x`、`127.x.x.x`、IPv6 本地地址等）进服时，不请求 API，直接显示：

  ```
  玩家 Allen 加入游戏 IP: 192.168.1.100 运营商: 内网地址 地理位置: 内网设备
  ```

- **运营商中文化**：自动将 API 返回的英文运营商名规范化为中文常用名，如 `China Mobile communications corporation` → `中国移动`、`Chinanet` → `中国电信`。
- 未配置 API Key 时使用访客免费额度，服务器启动时会提示：

  ```
  未配置 API Key，当前使用免费额度，请尽快配置 key（配置文件: plugins/IP_addr/config.json）
  ```

- 提供 `/ipinfo` 命令手动查询任意 IP 或域名。
- 提供 `/ipaddr reload` 命令重载配置。

## 安装

1. 构建插件（见下文），将 `bin/IP_addr/` 目录复制到 LeviLamina 服务端的 `plugins/` 目录下。
2. 启动服务器，会自动生成 `plugins/IP_addr/config.json`。
3. （可选）在 `config.json` 中填写 API Key。

## 配置

配置文件位于 `plugins/IP_addr/config.json`：

```json
{
  // uapis.cn 的 API Key，留空则使用访客免费额度
  // 申请地址: https://uapis.cn/console/api-keys
  "key": "",

  // 玩家加入服务器时，是否查询并在控制台输出 IP / 运营商 / 地理位置
  "enableJoinQuery": true,

  // 单次 HTTP 请求超时时间（毫秒）
  "timeoutMs": 5000,

  // 日志等级：-1 Off, 0 Fatal, 1 Error, 2 Warn, 3 Info, 4 Debug, 5 Trace
  "logLevel": 3,

  // SQLite 缓存数据库路径（相对于服务器根目录）
  "databasePath": "plugins/IP_addr/data/ip_cache.db",

  // IP 缓存保留天数：超过该天数未更新的记录在服务器启动时自动清理；<= 0 表示不清理
  "cacheRetentionDays": 90,

  // 查询失败时的重试次数（不含首次请求），应对网络波动造成的超时；0 表示不重试
  // 仅对网络失败、限流(429)、服务器错误(5xx)重试；参数错误、Key 无效等确定性错误不重试
  "retryCount": 5,

  // 重试基础延迟（毫秒），指数退避：500ms、1000ms、2000ms、2000ms ...
  "retryDelayMs": 500
}
```

> API Key 通过 `Authorization: Bearer <key>` 请求头传递。未配置时接口使用访客免费额度（4 次/秒限流 + 总额度上限，建议尽快配置 key）。

### 重试机制

遇到以下情况时自动重试（默认 5 次，指数退避 500ms → 1000ms → 2000ms ... 封顶 4 倍基础延迟）：

- **网络失败**：请求超时（WinHTTP 错误 12002）、连接失败、DNS 解析失败等网络波动
- **限流 429**：访客免费额度 4 次/秒的限制，退避后自动恢复
- **服务器错误 5xx**：API 服务端临时故障

以下确定性错误**不会**重试（重试无意义），直接返回失败原因：

- 400 参数错误（如非法 IP）
- 401 API Key 无效
- 未收录的 IP

每次重试前会在日志中输出提示，全部失败后的错误信息形如 `重试 5 次后仍失败: 网络请求失败: ...`。

### 缓存机制

插件使用 SQLite 数据库存储缓存（默认 `plugins/IP_addr/data/ip_cache.db`，可在配置中修改）：

| 表 | 说明 |
| --- | --- |
| `ip_cache` | IP/域名 → 查询结果。多个玩家使用同一 IP 时只存一条记录 |
| `player_ip` | 玩家 UUID → 上次的 IP。用于判断玩家 IP 是否发生变化 |

工作流程：

1. 玩家进服 → 若为内网地址，直接显示"内网地址 / 内网设备"，不查数据库不请求 API
2. 查 `player_ip`：UUID 对应的 IP 与本次一致 → 从 `ip_cache` 读取详情 → **命中缓存，不请求 API**（日志末尾带 `(缓存)` 标记）
3. IP 变化或首次进服 → 请求 API → 事务内同时更新两表

数据库优化：WAL 日志模式、预编译语句复用、写入事务、`updated_at` 索引 + 启动时自动清理超过 `cacheRetentionDays` 天的记录。

删除 `plugins/IP_addr/data/` 目录即可清空缓存。

## 命令

| 命令 | 权限 | 说明 |
| --- | --- | --- |
| `/ipinfo <IP或域名>` | 所有人 | 查询指定 IP / 域名的地理位置与运营商信息 |
| `/ipaddr reload` | 管理员 | 重载 `config.json` |

## 构建

依赖：[xmake](https://xmake.io/)、Visual Studio（C++ 桌面开发 + Clang 工具）或 LLVM。

```bash
# 构建服务端版本（默认）
xmake f -a x64 -m release -p windows --toolchain=clang-cl -y
xmake -w -y
```

构建产物位于 `bin/IP_addr/`，将其拷贝到服务端 `plugins/` 目录即可。

## 开发

项目结构：

```
src/
├── Entry/           # 插件入口（load/enable/disable）
├── Config/          # 配置管理（config.json）
├── Db/              # SQLite 缓存数据库
├── Network/         # WinHTTP 客户端、IP 查询服务、内网地址判断
└── Command/         # 命令注册
```

基于 [LeviLamina 模组开发教程](https://lamina.levimc.org/zh/developer_guides/tutorials/create_your_first_mod/) 制作。

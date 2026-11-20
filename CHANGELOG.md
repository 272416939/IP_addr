# Changelog

本文件遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 规范。

## [0.6.0] - 2026-11-20

### Changed

- 适配 LeviLamina 26.60.x（`add_requires("levilamina 26.60.*")`）。
- 包描述文件由 `tooth.json`（format 3）迁移到 `lip.toml`（format 4），与 LeviLamina 26.60.8 起的上游做法一致：依赖改为 `>=26.60.0 <26.61.0`，产物仍放置到 `mods/IP_addr/`（服务端的 `plugins/` 是指向 `mods/` 的目录联接），并新增 `preserve` 以在升级时保留 `config.json` 与 `data/` 缓存。
- 移除 0.5.0 时期针对 26.51.x 的本地 xmake 仓库 `on_install` 头文件补丁：LeviLamina 26.60.8 已修复「前向声明 + 容器成员」引发的 incomplete type 问题，直接使用官方 [xmake-repo](https://github.com/LiteLDev/xmake-repo) 即可构建，无需任何本地补丁。

### Notes

- 插件源码无需改动：在 26.60.8 下构建零警告零错误，未触发任何 `MCNAPI` 标记（`[[deprecated("This API is not available...")]]`），因此不需要提交 [mcapi-requests](https://github.com/LiteLDev/mcapi-requests/issues) 申请。
- 26.60.0 的破坏性变更（Packet 处理新增 `PacketSide` 参数、移除 `ll/api/network/packet/runtime/RuntimePacket.h`、移除 `ll::coro::InterruptableSleep`）与本插件无关——本插件只用玩家事件、命令、配置、日志与 SQLite。
- 玩家 IP 仍取自 `NetworkIdentifier::getIPAndPort()`（LeviLamina 自加的 `LLNDAPI` 接口，非 MCNAPI）：RakNet 连接走 `getRakPeer().GetSystemAddressFromGuid`，NetherNet 连接走 LL 内部 `ll::network::NetherNetInfo` 记录的候选地址；两者都取不到时上游回退为 `127.0.0.1:65535`（26.60 服务端默认使用 NetherNet 单端口，行为与 26.51.x 一致）。

## [0.5.0] - 2026-10-10

### Changed

- 适配 LeviLamina 26.51.x（`add_requires("levilamina 26.51.*")`），tooth.json 依赖同步更新为 `>=26.51.0 <26.52.0`。
- 本地 xmake 仓库的 levilamina 包定义增加了 `on_install` 补丁：为 26.51.x 自动生成头文件中「前向声明 + 容器成员」的类型补充完整包含（约 1600 个头文件），修复 clang-cl + 新版 MSVC STL 下的 incomplete type 编译错误；对存在循环依赖的 RakPeer/ShadowBanList 改用 .cpp 层面修复。

## [0.4.0] - 2026-10-10

### Added

- 查询失败自动重试机制（默认 5 次，可通过 `retryCount` 配置），指数退避延迟（`retryDelayMs`，默认 500ms，封顶 4 倍），应对网络波动造成的超时（如 WinHTTP 错误 12002）。
- 重试范围：网络失败、限流（429）、服务器错误（5xx）；确定性错误（400 参数错误、401 Key 无效、IP 未收录）不重试，快速返回。
- 每次重试前输出日志提示；全部失败后的错误信息包含重试次数。

## [0.3.0] - 2026-10-10

### Added

- 缓存存储由 JSON 文件迁移到 SQLite 数据库（`plugins/IP_addr/data/ip_cache.db`），支持 `databasePath`、`cacheRetentionDays` 配置项。
- 内网地址免查询：玩家使用内网 IP（10.x/172.16-31.x/192.168.x/127.x/169.254.x/100.64-127.x、IPv6 环回/链路本地/ULA 等）进服时，不请求 API，运营商显示"内网地址"，地理位置显示"内网设备"。

### Changed

- 数据库层优化：WAL 日志模式、synchronous=NORMAL、busy_timeout、全部热点 SQL 预编译复用、两表写入使用事务、updated_at 索引、启动时自动清理过期记录。
- ip_cache 表按 IP 去重，多个玩家共享同一 IP 只存一条记录。

## [0.2.0] - 2026-10-10

### Added

- IP 查询缓存：同一玩家 IP 未变化时读取缓存，不请求 API；IP 变化才重新查询。缓存持久化到 `plugins/IP_addr/ip_cache.json`。
- `/ipinfo` 命令重复查询同一 IP 时复用缓存。

### Changed

- 运营商名称规范化为中文常用名（China Mobile ... → 中国移动，Chinanet → 中国电信 等），isp 为空时回退使用 llc 字段。

### Fixed

- 修复 API 返回 429 限流响应（`{"code":429,...}`，code 为数字）时因 JSON 类型不匹配导致服务器崩溃的问题；限流时输出友好提示。
- 所有 JSON 字段均改为类型安全读取，异常响应不再导致未捕获异常。

## [0.1.0] - 2026-10-10

### Added

- 玩家加入服务器时，在控制台输出玩家 IP、运营商、地理位置。
- 支持通过 `config.json` 中的 `key` 配置 uapis.cn API Key；未配置时使用访客免费额度并在启动时提示。
- `/ipinfo <IP或域名>` 命令：手动查询 IP 信息。
- `/ipaddr reload` 命令：重载配置。



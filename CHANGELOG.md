# Changelog

本文件遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 规范。

## [0.1.0] - 2026-10-10

### Added

- 玩家加入服务器时，在控制台输出玩家 IP、运营商、地理位置。
- 支持通过 `config.json` 中的 `key` 配置 uapis.cn API Key；未配置时使用访客免费额度并在启动时提示。
- `/ipinfo <IP或域名>` 命令：手动查询 IP 信息。
- `/ipaddr reload` 命令：重载配置。

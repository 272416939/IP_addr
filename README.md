# IP_addr

LeviLamina 26.20.7 原生插件：玩家加入服务器时，在控制台输出玩家的 **IP 地址、运营商、地理位置**。

IP 信息来自 [uapis.cn](https://uapis.cn/docs/api-reference/get-network-ipinfo) 的 IP 查询 API。

## 功能

- 玩家加入服务器（登录）时，自动查询并在控制台输出：

  ```
  玩家 Allen 加入游戏 IP: 1.2.3.4 运营商: 电信 地理位置: 中国 广东 深圳
  ```

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

  // HTTP 请求超时时间（毫秒）
  "timeoutMs": 5000,

  // 日志等级：-1 Off, 0 Fatal, 1 Error, 2 Warn, 3 Info, 4 Debug, 5 Trace
  "logLevel": 3
}
```

> API Key 通过 `Authorization: Bearer <key>` 请求头传递。未配置时接口使用访客免费额度（有额度上限，建议尽快配置 key）。

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
├── Network/         # WinHTTP 客户端与 IP 查询服务
└── Command/         # 命令注册
```

基于 [LeviLamina 模组开发教程](https://lamina.levimc.org/zh/developer_guides/tutorials/create_your_first_mod/) 制作。

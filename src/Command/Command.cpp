#include "Command/Command.h"

#include "Config/ConfigManager.h"
#include "Network/IpCache.h"
#include "Network/IpService.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"

namespace IPA {

// /ipinfo <ip> 的参数
struct IpInfoParam {
    std::string ip;
};

// /ipaddr <action> 的子命令
enum class AdminAction { reload };

struct IpAddrParam {
    AdminAction action;
};

// 输出一条 IP 查询结果
void printIpInfo(CommandOutput& output, const IpInfo& info, std::string const& fallbackIp, bool fromCache) {
    output.success("IP 查询结果{}:", fromCache ? " (缓存)" : "");
    output.success("IP: {}", info.ip.empty() ? fallbackIp : info.ip);
    output.success("运营商: {}", info.isp.empty() ? "未知" : info.isp);
    output.success("地理位置: {}", info.region.empty() ? "未知" : info.region);
    if (!info.asn.empty()) output.success("ASN: {}", info.asn);
    if (!info.llc.empty()) output.success("网络所有者: {}", info.llc);
    if (!info.beginip.empty() && !info.endip.empty()) {
        output.success("IP 段: {} - {}", info.beginip, info.endip);
    }
}

void registerCommand() {
    auto& registrar = ll::command::CommandRegistrar::getInstance(false);

    // 手动注册枚举，确保 /ipaddr reload 可用
    registrar.tryRegisterEnum<AdminAction>();

    // /ipinfo <IP地址或域名> —— 查询 IP 信息
    auto& ipinfoCmd = registrar.getOrCreateCommand(
        "ipinfo",
        "查询 IP 地址的地理位置与运营商信息",
        CommandPermissionLevel::Any
    );

    ipinfoCmd.overload<IpInfoParam>().execute(
        [](CommandOrigin const&, CommandOutput& output, IpInfoParam const& param, Command const&) {
            auto&       cache  = IpCache::getInstance();
            const auto& config = ConfigManager::getInstance().get();

            // 重复查询同一 IP 时复用缓存
            if (auto cached = cache.getForIp(param.ip)) {
                printIpInfo(output, *cached, param.ip, true);
                return;
            }

            std::string error;
            auto        info = IpService::queryIp(param.ip, config.key, config.timeoutMs, error);
            if (info) {
                cache.putForIp(param.ip, *info);
                cache.save();
                printIpInfo(output, *info, param.ip, false);
            } else {
                output.error("IP 查询失败: {}", error);
            }
        }
    );

    ipinfoCmd.overload<ll::command::EmptyParam>().execute(
        [](CommandOrigin const&, CommandOutput& output, ll::command::EmptyParam const&, Command const&) {
            output.error("用法: /ipinfo <IP地址或域名>");
        }
    );

    // /ipaddr reload —— 重载配置文件
    auto& ipaddrCmd = registrar.getOrCreateCommand(
        "ipaddr",
        "IP_addr 插件管理命令",
        CommandPermissionLevel::GameDirectors
    );

    ipaddrCmd.overload<IpAddrParam>().execute(
        [](CommandOrigin const&, CommandOutput& output, IpAddrParam const& param, Command const&) {
            if (param.action == AdminAction::reload) {
                if (ConfigManager::getInstance().reload()) {
                    const auto& config = ConfigManager::getInstance().get();
                    if (config.key.empty()) {
                        output.success("配置已重载（当前未配置 API Key，使用免费额度）");
                    } else {
                        output.success("配置已重载（API Key 已配置）");
                    }
                } else {
                    output.error("配置重载失败，请检查配置文件");
                }
            }
        }
    );
}

} // namespace IPA

add_rules("mode.debug", "mode.release")

if is_plat("windows") then
    set_toolchains("clang-cl")
end

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

-- add_requires("levilamina x.x.x") for a specific version
-- add_requires("levilamina develop") to use develop version
if is_config("target_type", "server") then
    add_requires("levilamina 26.20.7", { configs = { target_type = "server" } })
else
    add_requires("levilamina 26.20.7", { configs = { target_type = "client" } })
end

add_requires("levibuildscript")
add_requires("sqlite3")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

option("target_type")
    set_default("server")
    set_showmenu(true)
    set_values("server", "client")
option_end()

target("IP_addr")

    set_default(true)
    add_rules("@levibuildscript/linkrule")

    add_rules("@levibuildscript/modpacker", {
        pluginName = "IP_addr",
        modFile    = "IP_addr.dll"
    })
    if is_plat("windows") then
        add_defines("NOMINMAX", "UNICODE")
        set_exceptions("cxx")
        add_cxflags("/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
        add_cxflags(
            "/EHs",
            "-Wno-microsoft-cast",
            "-Wno-invalid-offsetof",
            "-Wno-c++2b-extensions",
            "-Wno-microsoft-include",
            "-Wno-overloaded-virtual",
            "-Wno-ignored-qualifiers",
            "-Wno-missing-field-initializers",
            "-Wno-potentially-evaluated-expression",
            "-Wno-pragma-system-header-outside-header",
            { tools = { "clang_cl" } }
        )
        -- WinHTTP：用于发起 HTTPS 请求查询 IP 信息
        -- ws2_32：inet_pton 等 IP 地址解析函数
        add_syslinks("winhttp", "ws2_32")
    end
    add_packages("levilamina", "sqlite3")
    set_kind("shared")
    set_languages("c++23")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")
    if is_config("target_type", "server") then
        add_defines("LL_PLAT_S")
    else
        add_defines("LL_PLAT_C")
    end

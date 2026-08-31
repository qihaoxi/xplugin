include_guard(GLOBAL)

# ============================================================================
# cmake/buildinfo.cmake —— 强制链接器保留 build-info 符号
#
# 范式来源：xvm/cmake/buildinfo.cmake。为 xplugin 适配符号名（xpl_build_info）。
#
# 为什么需要：xplugin_core 是静态库，build_info.c 里的 xpl_build_info() 若未被
# 最终可执行文件直接引用，链接器的死代码消除会把整个 .o 丢掉，运行期便查不到
# 溯源信息。用 --undefined / -u / INCLUDE 把符号钉住。
#
# 用法：xpl_enable_buildinfo(<target>)   # 对最终可执行文件/共享库调用
# ============================================================================

option(XPLUGIN_KEEP_BUILDINFO "Force linker to retain xplugin build-info symbols" ON)

function(xpl_enable_buildinfo target)
    if (NOT TARGET ${target})
        message(FATAL_ERROR "xpl_enable_buildinfo: target '${target}' does not exist")
    endif ()
    if (NOT XPLUGIN_KEEP_BUILDINFO)
        return()
    endif ()

    if (APPLE)
        target_link_options(${target} PRIVATE "-Wl,-u,_xpl_build_info")
    elseif (UNIX)
        target_link_options(${target} PRIVATE "-Wl,--undefined=xpl_build_info")
    elseif (MSVC)
        target_link_options(${target} PRIVATE "/INCLUDE:xpl_build_info")
    endif ()
endfunction()

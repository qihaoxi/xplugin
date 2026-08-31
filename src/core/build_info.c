/*
 * build_info.c —— 构建溯源信息的唯一权威实现（design §4.7 类型 A：struct shadow）
 *
 * 结构体 `struct xpl_build_info` 布局私有（仅本文件可见）：字段增改/重排不影响
 * 公共 ABI,新增暴露字段 = 新增访问器函数（尾部追加符号,兼容）。
 * 数据来自 cmake/git.cmake 生成的 include/xplugin/xplugin_build_info_generated.h
 * （configure 时采集,gitignored）;语义版本串直接取 xplugin.h 的
 * XPLUGIN_VERSION_STRING（header 宏,无额外注入）。
 * 符号保留由 cmake/buildinfo.cmake 钉住（防静态库死代码消除丢弃本 .o）。
 */
#include "xplugin/xplugin.h"

#include "xplugin/xplugin_build_info_generated.h"

struct xpl_build_info {
	const char* git_commit;
	const char* git_branch;
	const char* timestamp;
	const char* os;
	const char* distro;
	const char* distro_version;
	const char* distro_codename;
	const char* kernel;
	const char* arch;
	const char* host;
	const char* toolchain;
	const char* cc;
	const char* cc_path;
	const char* linker;
	const char* version;
};

static const struct xpl_build_info g_xpl_build_info = {
	XPL_BUILD_GIT_COMMIT, XPL_BUILD_GIT_BRANCH,		XPL_BUILD_TIMESTAMP,	   XPL_BUILD_OS,
	XPL_BUILD_DISTRO,	  XPL_BUILD_DISTRO_VERSION, XPL_BUILD_DISTRO_CODENAME, XPL_BUILD_KERNEL,
	XPL_BUILD_ARCH,		  XPL_BUILD_HOST,			XPL_BUILD_TOOLCHAIN,	   XPL_BUILD_CC,
	XPL_BUILD_CC_PATH,	  XPL_BUILD_LINKER,			XPLUGIN_VERSION_STRING,
};

const xpl_build_info_t* xpl_build_info(void) {
	return &g_xpl_build_info;
}

/* ── 逐字段访问器（与 xplugin.h 声明一一对应;加字段在此加一条宏调用） ── */
#define XPLUGIN_BI_ACCESSOR(name)                                                                                      \
	const char* xpl_build_info_##name(const xpl_build_info_t* info) {                                                  \
		return info->name;                                                                                             \
	}

XPLUGIN_BI_ACCESSOR(git_commit)
XPLUGIN_BI_ACCESSOR(git_branch)
XPLUGIN_BI_ACCESSOR(timestamp)
XPLUGIN_BI_ACCESSOR(os)
XPLUGIN_BI_ACCESSOR(distro)
XPLUGIN_BI_ACCESSOR(distro_version)
XPLUGIN_BI_ACCESSOR(distro_codename)
XPLUGIN_BI_ACCESSOR(kernel)
XPLUGIN_BI_ACCESSOR(arch)
XPLUGIN_BI_ACCESSOR(host)
XPLUGIN_BI_ACCESSOR(toolchain)
XPLUGIN_BI_ACCESSOR(cc)
XPLUGIN_BI_ACCESSOR(cc_path)
XPLUGIN_BI_ACCESSOR(linker)
XPLUGIN_BI_ACCESSOR(version)

#undef XPLUGIN_BI_ACCESSOR

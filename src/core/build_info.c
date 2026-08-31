/*
 * build_info.c —— 构建溯源信息的唯一权威实现
 *
 * 数据来自 cmake/git.cmake 生成的 xplugin_build_info_generated.h（configure 时采集）；
 * 语义版本串直接取 xplugin.h 的 XPLUGIN_VERSION_STRING（header 宏，无额外注入）。
 * 符号保留由 cmake/buildinfo.cmake 钉住（防静态库死代码消除丢弃本 .o）。
 */
#include "xplugin/xplugin.h"

#include "xplugin_build_info_generated.h" /* 生成于 ${CMAKE_BINARY_DIR}/generated/xplugin */

static const xpl_build_info_t g_xpl_build_info = {
	XPL_BUILD_GIT_COMMIT, XPL_BUILD_GIT_BRANCH, XPL_BUILD_TIMESTAMP,	XPL_BUILD_HOST,
	XPL_BUILD_SYSTEM,	  XPL_BUILD_COMPILER,	XPLUGIN_VERSION_STRING,
};

const xpl_build_info_t* xpl_build_info(void) {
	return &g_xpl_build_info;
}

/*
 * xplugin.h —— xplugin 核心公共头（唯一入口）
 *
 * M0：版本/ABI 契约 + 导出宏。M1 起在此追加核心 API（docs/design.md §5）。
 * 公共面纪律：xplugin 对消费方零反向依赖；本头自足可单独 include。
 */
#ifndef XPLUGIN_XPLUGIN_H
#define XPLUGIN_XPLUGIN_H

/* ── 库版本与 ABI 契约（design §4.1） ── */
#define XPLUGIN_VERSION_MAJOR 0
#define XPLUGIN_VERSION_MINOR 1
#define XPLUGIN_VERSION_PATCH 0
/* ABI 契约版本：破坏兼容的变更（删符号/改签名/动前置字段）必须 +1 */
#define XPLUGIN_ABI_VERSION 1

#define XPLUGIN_STRINGIFY_(v) #v
#define XPLUGIN_STRINGIFY(v) XPLUGIN_STRINGIFY_(v)
#define XPLUGIN_VERSION_STRING                                                                                         \
	XPLUGIN_STRINGIFY(XPLUGIN_VERSION_MAJOR)                                                                           \
	"." XPLUGIN_STRINGIFY(XPLUGIN_VERSION_MINOR) "." XPLUGIN_STRINGIFY(XPLUGIN_VERSION_PATCH)

/* ── 导出符号（静态库下为空；loader 档单拆 .so 时控制可见性，design §7） ── */
#if defined(_WIN32)
#if defined(XPLUGIN_BUILDING_DLL)
#define XPL_API __declspec(dllexport)
#elif defined(XPLUGIN_USE_DLL)
#define XPL_API __declspec(dllimport)
#else
#define XPL_API
#endif
#else
#define XPL_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 返回 "major.minor.patch" 库版本字符串，与 XPLUGIN_* 宏恒一致 */
XPL_API const char* xpl_version(void);

#ifdef __cplusplus
}
#endif

#endif /* XPLUGIN_XPLUGIN_H */

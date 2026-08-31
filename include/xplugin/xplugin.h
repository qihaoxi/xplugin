/*
 * xplugin.h —— xplugin 核心公共头（唯一入口）
 *
 * M1a：错误模型 + 分配器注入 + 日志 sink + xpl_ctx / effect 栈。
 * 公共面纪律：xplugin 对消费方零反向依赖；本头自足可单独 include。
 * 完整设计：docs/design.md（§4 公共契约，§5.1 ctx+effect）。
 */
#ifndef XPLUGIN_XPLUGIN_H
#define XPLUGIN_XPLUGIN_H

#include <stdarg.h>
#include <stddef.h>

/* ── 库版本与 ABI 契约（design §4.1） ── */
#define XPLUGIN_VERSION_MAJOR 1
#define XPLUGIN_VERSION_MINOR 0
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

/* ── 错误模型（design §4.2）：全 API 返回 xpl_status；查询类返回 NULL+last_error ── */
typedef enum xpl_status {
	XPL_OK = 0,		   /* 成功 */
	XPL_ENOMEM = 1,	   /* 分配失败（经注入分配器） */
	XPL_EINVAL = 2,	   /* 参数非法（NULL 句柄/undo、非法值等） */
	XPL_ENOTFOUND = 3, /* 服务/组件/事件不存在（M1b 起使用） */
	XPL_EVERSION = 4,  /* ABI 版本握手失败（M1b 起使用） */
	XPL_ECYCLE = 5,	   /* 依赖环（M1d 起使用） */
	XPL_EDUP = 6,	   /* 服务名/组件重复提供（M1b/M1d 使用） */
	XPL_EDEP = 7,	   /* 依赖缺失（M1d 使用） */
	XPL_EFAILED = 8,   /* 组件 install 回调返回失败（M1d 使用） */
	XPL_ECAP = 9,	   /* 容量耗尽（保留；当前增长失败统一走 ENOMEM） */
	XPL_ESTALE = 10,   /* remote：子进程失效/通道断开（M4 使用） */
	XPL_EPROTO = 11	   /* remote：协议帧错误（M4 使用） */
} xpl_status;

/* ── 日志级别（xpl_log_fn 的 level 参数） ── */
enum { XPL_LOG_ERROR = 1, XPL_LOG_WARN = 2, XPL_LOG_INFO = 3, XPL_LOG_DEBUG = 4 };

/* ── 分配器注入（design §4.3）：框架全部堆分配唯一收口；传 NULL = libc 默认 ── */
typedef struct xpl_allocator {
	void* (*alloc)(void* user, size_t size);
	void* (*realloc)(void* user, void* p, size_t size);
	void (*free)(void* user, void* p);
	void* user;
} xpl_allocator;

/* ── 日志 sink（design §4.6）：默认静默；库内任何输出必经此，不直接打 stdout/stderr ── */
typedef void (*xpl_log_fn)(void* user, int level, const char* fmt, va_list ap);

/* ── 上下文（design §5.1）：实例根容器，MT 不安全；生命周期流转唯一经 CAS ── */
typedef struct xpl_ctx xpl_ctx;

XPL_API xpl_ctx* xpl_ctx_new(const xpl_allocator* a); /* 分配失败返回 NULL */
XPL_API xpl_status xpl_ctx_destroy(xpl_ctx* ctx);	  /* NULL → no-op；重入返回 XPL_EINVAL */

/* 最近一次错误详情（无错误返回空串）；buf 可空、n==0 时 no-op */
XPL_API void xpl_ctx_last_error(xpl_ctx* ctx, char* buf, size_t n);

XPL_API void xpl_ctx_set_log(xpl_ctx* ctx, xpl_log_fn fn, void* user);

/* ── effect 栈（design §5.1）：注册类 API 自动 push；自定义资源（文件句柄等）手写 ── */
typedef void (*xpl_undo_fn)(xpl_ctx* ctx, void* userdata);

XPL_API xpl_status xpl_effect_push(xpl_ctx* ctx, xpl_undo_fn undo, void* userdata);
XPL_API size_t xpl_effect_mark(xpl_ctx* ctx); /* 回滚水位（install 事务用） */
XPL_API xpl_status xpl_effect_rollback_to(xpl_ctx* ctx, size_t mark);

/* ── 构建溯源（provenance，design §4.1 互补面）：configure 时采集，编译进产物 ── */
typedef struct xpl_build_info {
	const char* git_commit; /* 完整 hash，dirty 时带 "-dirty"；无 git 为 "nogit" */
	const char* git_branch;
	const char* timestamp;		 /* ISO8601 构建时刻 */
	const char* os;				 /* 操作系统名，如 "Linux" */
	const char* distro;			 /* 发行版名，如 "Debian GNU/Linux"；非发行版 = os */
	const char* distro_version;	 /* 发行版版本号，如 "12"；无则 "-" */
	const char* distro_codename; /* 发行版代号，如 "bookworm"；无则 "-" */
	const char* kernel;			 /* 内核版本，如 "6.1.0-amd64" */
	const char* arch;			 /* 架构，如 "x86_64" */
	const char* host;			 /* 构建主机名（nodename） */
	const char* compiler;		 /* 编译器名+版本，如 "GNU 15.2.0" */
	const char* compiler_path;	 /* 编译器可执行路径 */
	const char* linker;			 /* 链接器名+版本，如 "GNU ld (GNU Binutils for Debian) 2.44" */
	const char* version;		 /* 语义版本串（恒等于 XPLUGIN_VERSION_STRING） */
} xpl_build_info_t;

/* 返回指向静态存储的构建信息（永不 NULL，永不失败） */
XPL_API const xpl_build_info_t* xpl_build_info(void);

/* 返回 "major.minor.patch" 库版本字符串，与 XPLUGIN_* 宏恒一致 */
XPL_API const char* xpl_version(void);

#ifdef __cplusplus
}
#endif

#endif /* XPLUGIN_XPLUGIN_H */

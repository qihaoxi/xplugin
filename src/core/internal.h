/*
 * internal.h —— core 内部共享（不出组件）
 * ctx 结构体内存布局 + 收口点声明（design §4.2/§4.4）。
 */
#ifndef XPLUGIN_SRC_CORE_INTERNAL_H
#define XPLUGIN_SRC_CORE_INTERNAL_H

#include "xplugin/xplugin.h"

#include <stdatomic.h>
#include <stdarg.h>

/* ── 生命周期状态（design §4.4）：流转唯一经 xpl_lifecycle_cas ── */
enum {
	XPL_LS_NEW = 0,
	XPL_LS_DESTROYING = 1,
	XPL_LS_DESTROYED = 2,
};

typedef struct xpl_effect {
	xpl_undo_fn undo;
	void* userdata;
} xpl_effect;

/* 容忍错误详情上限；动态 buffer 会引入第二次分配与失败面，固定缓冲为保证确定性 */
#define XPL_ERROR_BUF_SIZE 160

struct xpl_ctx {
	xpl_allocator alloc; /* 注入分配器（NULL → libc 包装），框架全部分配唯一收口 */
	xpl_effect* effects;
	size_t effect_cap;
	size_t effect_len;
	xpl_status last_status;
	char last_error[XPL_ERROR_BUF_SIZE];
	int has_error;
	xpl_log_fn log_fn;
	void* log_user;
	_Atomic int lifecycle;
};

/* 生命周期 CAS 收口点：from→to 成功返回 1，否则 0（败者 no-op） */
int xpl_lifecycle_cas(xpl_ctx* ctx, int from, int to);
/* 判断是否处于不可在 push/注册 的状态（DESTROYING/DESTROYED） */
int xpl_lifecycle_locked(xpl_ctx* ctx);

/* 日志与错误记录（内部收口，勿在组件外直接用） */
void xpl_log(xpl_ctx* ctx, int level, const char* fmt, ...);
void xpl_set_error(xpl_ctx* ctx, xpl_status st, const char* fmt, ...);

/* effect 栈内部回放：从栈顶逆序回放到 upto（不含）。rollback 与 destroy 共用 */
void xpl_effect_unwind(xpl_ctx* ctx, size_t upto);

#endif /* XPLUGIN_SRC_CORE_INTERNAL_H */

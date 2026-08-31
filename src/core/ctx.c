#include "xplugin/xplugin.h"
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── libc 默认分配器包装 ── */
static void* libc_alloc(void* user, size_t size) {
	(void)user;
	return malloc(size);
}

static void* libc_realloc(void* user, void* p, size_t size) {
	(void)user;
	return realloc(p, size);
}

static void libc_free(void* user, void* p) {
	(void)user;
	free(p);
}

static const xpl_allocator xpl_libc_allocator = {
	libc_alloc,
	libc_realloc,
	libc_free,
	NULL,
};

/* ════════════ 内部收口点 ════════════ */

int xpl_lifecycle_cas(xpl_ctx* ctx, int from, int to) {
	int expected = from;
	return atomic_compare_exchange_strong(&ctx->lifecycle, &expected, to);
}

int xpl_lifecycle_locked(xpl_ctx* ctx) {
	int ls = atomic_load(&ctx->lifecycle);
	return ls == XPL_LS_DESTROYING || ls == XPL_LS_DESTROYED;
}

void xpl_set_error(xpl_ctx* ctx, xpl_status st, const char* fmt, ...) {
	ctx->last_status = st;
	ctx->has_error = 1;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(ctx->last_error, sizeof ctx->last_error, fmt, ap);
	va_end(ap);
}

void xpl_log(xpl_ctx* ctx, int level, const char* fmt, ...) {
	if (!ctx || !ctx->log_fn)
		return;
	va_list ap;
	va_start(ap, fmt);
	ctx->log_fn(ctx->log_user, level, fmt, ap);
	va_end(ap);
}

/* ════════════ 生命周期 ════════════ */

xpl_ctx* xpl_ctx_new(const xpl_allocator* a) {
	const xpl_allocator* al = a ? a : &xpl_libc_allocator;
	xpl_ctx* ctx = (xpl_ctx*)al->alloc(al->user, sizeof *ctx);
	if (!ctx)
		return NULL;
	memset(ctx, 0, sizeof *ctx);
	ctx->alloc = *al;
	atomic_init(&ctx->lifecycle, XPL_LS_NEW);
	/* 起步 16 槽：首次 push 零分配，行为确定（design §6 成本模型） */
	ctx->effect_cap = 16;
	ctx->effects = (xpl_effect*)al->alloc(al->user, sizeof(xpl_effect) * ctx->effect_cap);
	if (!ctx->effects) {
		al->free(al->user, ctx);
		return NULL;
	}
	return ctx;
}

xpl_status xpl_ctx_destroy(xpl_ctx* ctx) {
	if (!ctx)
		return XPL_OK; /* destroy NULL → no-op（design §5.1 规约） */
	if (!xpl_lifecycle_cas(ctx, XPL_LS_NEW, XPL_LS_DESTROYING))
		return XPL_EINVAL; /* 销毁中/已销毁 → 重入拒绝 */

	xpl_effect_unwind(ctx, 0); /* 逆序回放全部 effect */
	const xpl_allocator* al = &ctx->alloc;
	al->free(al->user, ctx->effects);
	al->free(al->user, ctx);
	return XPL_OK;
}

void xpl_ctx_last_error(xpl_ctx* ctx, char* buf, size_t n) {
	if (!ctx || !buf || n == 0)
		return;
	const char* src = ctx->has_error ? ctx->last_error : "";
	snprintf(buf, n, "%s", src);
}

void xpl_ctx_set_log(xpl_ctx* ctx, xpl_log_fn fn, void* user) {
	if (!ctx)
		return;
	ctx->log_fn = fn;
	ctx->log_user = user;
}

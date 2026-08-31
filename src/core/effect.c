#include "xplugin/xplugin.h"
#include "internal.h"

/* 逆序回放：从栈顶一路降到 upto（不含）。rollback 与 destroy 共用（design §12-5） */
void xpl_effect_unwind(xpl_ctx* ctx, size_t upto) {
	while (ctx->effect_len > upto) {
		xpl_effect e = ctx->effects[--ctx->effect_len];
		if (e.undo)
			e.undo(ctx, e.userdata);
	}
}

xpl_status xpl_effect_push(xpl_ctx* ctx, xpl_undo_fn undo, void* userdata) {
	if (!ctx) {
		return XPL_EINVAL;
	}
	if (!undo) {
		xpl_set_error(ctx, XPL_EINVAL, "effect_push: undo 为 NULL");
		xpl_log(ctx, XPL_LOG_WARN, "xpl_effect_push: NULL undo ignored");
		return XPL_EINVAL;
	}
	if (xpl_lifecycle_locked(ctx)) {
		xpl_set_error(ctx, XPL_EINVAL, "effect_push: ctx 已在销毁中");
		return XPL_EINVAL;
	}

	if (ctx->effect_len == ctx->effect_cap) {
		size_t ncap = ctx->effect_cap * 2;
		xpl_effect* ne = (xpl_effect*)ctx->alloc.realloc(ctx->alloc.user, ctx->effects, sizeof(xpl_effect) * ncap);
		if (!ne) {
			xpl_set_error(ctx, XPL_ENOMEM, "effect_push: 扩容失败 (cap %zu)", ncap);
			return XPL_ENOMEM;
		}
		ctx->effects = ne;
		ctx->effect_cap = ncap;
	}

	ctx->effects[ctx->effect_len].undo = undo;
	ctx->effects[ctx->effect_len].userdata = userdata;
	ctx->effect_len++;
	return XPL_OK;
}

size_t xpl_effect_mark(xpl_ctx* ctx) {
	return ctx ? ctx->effect_len : 0;
}

xpl_status xpl_effect_rollback_to(xpl_ctx* ctx, size_t mark) {
	if (!ctx) {
		return XPL_EINVAL;
	}
	if (mark > ctx->effect_len) {
		xpl_set_error(ctx, XPL_EINVAL, "rollback_to: mark %zu > 当前栈深 %zu", mark, ctx->effect_len);
		return XPL_EINVAL;
	}
	xpl_effect_unwind(ctx, mark);
	return XPL_OK;
}

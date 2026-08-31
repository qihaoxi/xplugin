/*
 * test_ctx.c —— xpl_ctx 生命周期 substrate
 * 覆盖：libc/注入分配器配平、错误查询、日志 sink、destroy 重入拒绝。
 */
#include "xplugin/xplugin.h"
#include "test_util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ∷ 计数分配器：统计 alloc/free 次数，destroy 后必须配平 */
typedef struct {
	long allocs;
	long frees;
} tcounter;

static void* t_alloc(void* user, size_t size) {
	tcounter* c = (tcounter*)user;
	(void)size;
	c->allocs++;
	return malloc(size);
}

static void* t_realloc(void* user, void* p, size_t size) {
	tcounter* c = (tcounter*)user;
	(void)c;
	return realloc(p, size);
}

static void t_free(void* user, void* p) {
	tcounter* c = (tcounter*)user;
	c->frees++;
	free(p);
}

static void test_default_libc_ctx(void) {
	xpl_ctx* k = xpl_ctx_new(NULL);
	XPL_TEST_CHECK(k != NULL);
	XPL_TEST_CHECK(xpl_ctx_destroy(k) == XPL_OK);
	XPL_TEST_CHECK(xpl_ctx_destroy(NULL) == XPL_OK); /* NULL → no-op */
}

static void test_injected_allocator_balanced(void) {
	tcounter c = { 0, 0 };
	xpl_allocator a = { t_alloc, t_realloc, t_free, &c };

	xpl_ctx* k = xpl_ctx_new(&a);
	XPL_TEST_CHECK(k != NULL);
	XPL_TEST_CHECK(c.allocs >= 2); /* ctx + effect 栈 */
	XPL_TEST_CHECK(xpl_ctx_destroy(k) == XPL_OK);
	XPL_TEST_CHECK(c.allocs == c.frees); /* destroy 后无泄漏 */
}

static void test_last_error(void) {
	char buf[128];
	xpl_ctx* k = xpl_ctx_new(NULL);

	xpl_ctx_last_error(k, buf, sizeof buf);
	XPL_TEST_CHECK(buf[0] == '\0'); /* 无错误 → 空串 */

	XPL_TEST_CHECK(xpl_effect_push(k, NULL, NULL) == XPL_EINVAL); /* NULL undo */
	xpl_ctx_last_error(k, buf, sizeof buf);
	XPL_TEST_CHECK(strstr(buf, "undo") != NULL); /* 错误详情可查 */

	xpl_ctx_destroy(k);
}

/* 日志 sink 捕获：got 计数、got_level 记录级别 */
static int log_got = 0;
static int log_got_level = 0;

static void log_cb(void* user, int level, const char* fmt, va_list ap) {
	(void)fmt;
	(void)ap;
	log_got++;
	log_got_level = level;
}

static void test_log_sink(void) {
	xpl_ctx* k = xpl_ctx_new(NULL);
	xpl_ctx_set_log(k, log_cb, NULL);

	/* 触发一次 WARN（NULL undo 路径） */
	xpl_effect_push(k, NULL, NULL);
	XPL_TEST_CHECK(log_got == 1);
	XPL_TEST_CHECK(log_got_level == XPL_LOG_WARN);

	/* 未设 sink 的 ctx：静默，无输出副作用 */
	xpl_ctx* silent = xpl_ctx_new(NULL);
	xpl_effect_push(silent, NULL, NULL);
	XPL_TEST_CHECK(log_got == 1); /* 未增量 */
	xpl_ctx_destroy(silent);

	xpl_ctx_destroy(k);
}

/* ∷ destroy 重入：undo 回调里再调 destroy 必须被拒（DESTROYING） */
static int reentrancy_blocked = 0;

static void undo_destroy_reenter(xpl_ctx* ctx, void* userdata) {
	(void)userdata;
	reentrancy_blocked = (xpl_ctx_destroy(ctx) == XPL_EINVAL) ? 1 : 0;
}

static void test_destroy_reentrancy(void) {
	xpl_ctx* k = xpl_ctx_new(NULL);
	XPL_TEST_CHECK(xpl_effect_push(k, undo_destroy_reenter, NULL) == XPL_OK);
	XPL_TEST_CHECK(xpl_ctx_destroy(k) == XPL_OK);
	XPL_TEST_CHECK(reentrancy_blocked == 1); /* 重入被生命周期 CAS 拦下 */
}

int main(void) {
	test_default_libc_ctx();
	test_injected_allocator_balanced();
	test_last_error();
	test_log_sink();
	test_destroy_reentrancy();
	XPL_TEST_DONE();
}

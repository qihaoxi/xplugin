/*
 * test_effect.c —— effect 可逆副作用栈 substrate
 * 覆盖：逆序回放、rollback_to 水位、非法 mark、扩容、分配失败、多 ctx 独立。
 */
#include "xplugin/xplugin.h"
#include "test_util.h"

#include <stdlib.h>
#include <string.h>

/* ∷ 回放顺序记录 */
#define SEQ_MAX 2048
static int seq[SEQ_MAX];
static int seq_len;

static void reset_seq(void) {
	seq_len = 0;
}

static void record(int v) {
	if (seq_len < SEQ_MAX)
		seq[seq_len++] = v;
}

typedef struct {
	int id;
} tag;

static void undo_record(xpl_ctx* ctx, void* userdata) {
	(void)ctx;
	record(userdata ? ((tag*)userdata)->id : -1); /* NULL userdata = 无标记 */
}

static void test_reverse_unwind_on_destroy(void) {
	xpl_ctx* k = xpl_ctx_new(NULL);
	tag t[5];
	for (int i = 0; i < 5; i++) {
		t[i].id = i;
		XPL_TEST_CHECK(xpl_effect_push(k, undo_record, &t[i]) == XPL_OK);
	}
	reset_seq();
	XPL_TEST_CHECK(xpl_ctx_destroy(k) == XPL_OK);
	/* 后注册先撤销 */
	XPL_TEST_CHECK(seq_len == 5);
	for (int i = 0; i < 5; i++)
		XPL_TEST_CHECK(seq[i] == 4 - i);
}

static void test_rollback_to_mark(void) {
	/* push a,b → mark → push c,d → rollback(mark) → 只剩 a,b */
	xpl_ctx* k = xpl_ctx_new(NULL);
	tag u[4];
	for (int i = 0; i < 2; i++) {
		u[i].id = i;
		xpl_effect_push(k, undo_record, &u[i]);
	}
	size_t m = xpl_effect_mark(k); /* 2 */
	for (int i = 2; i < 4; i++) {
		u[i].id = i;
		xpl_effect_push(k, undo_record, &u[i]);
	}

	reset_seq();
	XPL_TEST_CHECK(xpl_effect_rollback_to(k, m) == XPL_OK);
	XPL_TEST_CHECK(xpl_effect_mark(k) == m);
	XPL_TEST_CHECK(seq_len == 2);
	XPL_TEST_CHECK(seq[0] == 3 && seq[1] == 2); /* d,c 逆序撤销，a,b 保留 */

	reset_seq();
	xpl_ctx_destroy(k);
	XPL_TEST_CHECK(seq_len == 2);
	XPL_TEST_CHECK(seq[0] == 1 && seq[1] == 0); /* b,a 逆序 */
}

static void test_invalid_mark(void) {
	xpl_ctx* k = xpl_ctx_new(NULL);
	XPL_TEST_CHECK(xpl_effect_rollback_to(k, 1) == XPL_EINVAL); /* mark > 栈深 */
	xpl_ctx_destroy(k);
}

static void test_capacity_growth(void) {
	xpl_ctx* k = xpl_ctx_new(NULL);
	tag* t = (tag*)malloc(1000 * sizeof *t);
	XPL_TEST_CHECK(t != NULL);
	for (int i = 0; i < 1000; i++) {
		t[i].id = i % 100; /* id 重复不碍事，只验证回放总数与逆序 */
		XPL_TEST_CHECK(xpl_effect_push(k, undo_record, &t[i]) == XPL_OK);
	}
	reset_seq();
	xpl_ctx_destroy(k);
	XPL_TEST_CHECK(seq_len == 1000);
	XPL_TEST_CHECK(seq[0] == 99 && seq[999] == 0);
	free(t);
}

/* ∷ realloc 恒失败的分配器：push 扩容到 ENOMEM，ctx 仍可用 */
static void* fr_alloc(void* user, size_t size) {
	(void)user;
	return malloc(size);
}

static void* fr_realloc(void* user, void* p, size_t size) {
	(void)user;
	(void)p;
	(void)size;
	return NULL;
}

static void fr_free(void* user, void* p) {
	(void)user;
	free(p);
}

static void test_grow_allocation_failure(void) {
	xpl_allocator a = { fr_alloc, fr_realloc, fr_free, NULL };
	xpl_ctx* k = xpl_ctx_new(&a);
	XPL_TEST_CHECK(k != NULL);
	/* 起步槽 16：17 次 push 触发扩容 → 失败 */
	for (int i = 0; i < 16; i++) {
		XPL_TEST_CHECK(xpl_effect_push(k, undo_record, NULL) == XPL_OK);
	}
	XPL_TEST_CHECK(xpl_effect_push(k, undo_record, NULL) == XPL_ENOMEM);

	char buf[64];
	xpl_ctx_last_error(k, buf, sizeof buf);
	XPL_TEST_CHECK(buf[0] != '\0');

	/* 失败后 ctx 仍完整可用：destroy 走 16 个真实 undo */
	reset_seq();
	XPL_TEST_CHECK(xpl_ctx_destroy(k) == XPL_OK);
	XPL_TEST_CHECK(seq_len == 16);
}

/* ∷ alloc 恒失败的分配器：ctx_new 返回 NULL */
static void* fa_alloc(void* user, size_t size) {
	(void)user;
	(void)size;
	return NULL;
}

static void* fa_realloc(void* user, void* p, size_t size) {
	(void)user;
	(void)p;
	(void)size;
	return NULL;
}

static void fa_free(void* user, void* p) {
	(void)user;
	(void)p;
}

static void test_ctx_new_allocation_failure(void) {
	xpl_allocator a = { fa_alloc, fa_realloc, fa_free, NULL };
	XPL_TEST_CHECK(xpl_ctx_new(&a) == NULL);
}

/* ∷ 多 ctx 独立：互不干扰地各自回放 */
static void test_multi_ctx_independent(void) {
	xpl_ctx* a = xpl_ctx_new(NULL);
	xpl_ctx* b = xpl_ctx_new(NULL);
	tag ta[3], tb[3];
	for (int i = 0; i < 3; i++) {
		ta[i].id = i;
		tb[i].id = 10 + i;
		xpl_effect_push(a, undo_record, &ta[i]);
		xpl_effect_push(b, undo_record, &tb[i]);
	}
	reset_seq();
	xpl_ctx_destroy(a); /* 只收 a 的 3 个 */
	XPL_TEST_CHECK(seq_len == 3);
	XPL_TEST_CHECK(seq[0] == 2 && seq[2] == 0);

	reset_seq();
	xpl_ctx_destroy(b); /* b 仍完整 */
	XPL_TEST_CHECK(seq_len == 3);
	XPL_TEST_CHECK(seq[0] == 12 && seq[2] == 10);
}

int main(void) {
	test_reverse_unwind_on_destroy();
	test_rollback_to_mark();
	test_invalid_mark();
	test_capacity_growth();
	test_grow_allocation_failure();
	test_ctx_new_allocation_failure();
	test_multi_ctx_independent();
	XPL_TEST_DONE();
}

/*
 * test_util.h —— 极简测试助手：计数断言 + 收尾报告
 *
 * 模式：每个用例一个函数，main 里逐个调用，末尾 XPL_TEST_DONE()。
 * 失败不中断（XPL_TEST_CHECK 计数继续），收尾统一报告退出码。
 * 回归纪律（齐 xvm）：ASan/TSan 打底，见 cmake/sanitizer.cmake。
 */
#ifndef XPLUGIN_TESTS_TEST_UTIL_H
#define XPLUGIN_TESTS_TEST_UTIL_H

#include <stdio.h>

static int xpl_test_failures = 0;

#define XPL_TEST_CHECK(cond)                                                                                           \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
			xpl_test_failures++;                                                                                       \
		}                                                                                                              \
	} while (0)

/* 收尾：return 语义，须用在 int main() 末尾 */
#define XPL_TEST_DONE()                                                                                                \
	do {                                                                                                               \
		if (xpl_test_failures == 0) {                                                                                  \
			puts("OK");                                                                                                \
			return 0;                                                                                                  \
		}                                                                                                              \
		fprintf(stderr, "%d failure(s)\n", xpl_test_failures);                                                         \
		return 1;                                                                                                      \
	} while (0)

#endif /* XPLUGIN_TESTS_TEST_UTIL_H */

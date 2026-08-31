/*
 * test_smoke.c —— M0 骨架冒烟：公共头自足、版本契约可编译、链接 xplugin_core。
 * M1a 起在此追加真实功能测试（test_ctx / test_effect ...）。
 */
#include "xplugin/xplugin.h"
#include "test_util.h"

#include <stdio.h>
#include <string.h>

_Static_assert(XPLUGIN_ABI_VERSION == 1, "ABI contract v1");

static const char* version_to_string(void) {
	static char buf[32];
	snprintf(buf, sizeof buf, "%d.%d.%d", XPLUGIN_VERSION_MAJOR, XPLUGIN_VERSION_MINOR, XPLUGIN_VERSION_PATCH);
	return buf;
}

static void test_version_string(void) {
	const char* v = xpl_version();
	XPL_TEST_CHECK(v != NULL);
	if (v)
		XPL_TEST_CHECK(strcmp(v, XPLUGIN_VERSION_STRING) == 0);
}

static void test_version_macros_consistent(void) {
	/* 宏拼接出的字符串必须等于 xpl_version() 返回串 */
	XPL_TEST_CHECK(strcmp(version_to_string(), xpl_version()) == 0);
}

static void test_build_info(void) {
	const xpl_build_info_t* bi = xpl_build_info();
	XPL_TEST_CHECK(bi != NULL);
	/* struct shadow:布局私有,只经访问器读(docs/design.md §4.7 类型 A) */
	XPL_TEST_CHECK(xpl_build_info_version(bi) != NULL &&
				   strcmp(xpl_build_info_version(bi), XPLUGIN_VERSION_STRING) == 0);
	XPL_TEST_CHECK(xpl_build_info_git_commit(bi) != NULL && xpl_build_info_git_commit(bi)[0] != '\0');
	XPL_TEST_CHECK(xpl_build_info_git_branch(bi) != NULL && xpl_build_info_git_branch(bi)[0] != '\0');
	XPL_TEST_CHECK(xpl_build_info_timestamp(bi) != NULL && xpl_build_info_timestamp(bi)[0] != '\0');
	XPL_TEST_CHECK(xpl_build_info_os(bi) != NULL && xpl_build_info_os(bi)[0] != '\0');
	XPL_TEST_CHECK(xpl_build_info_distro(bi) != NULL && xpl_build_info_distro(bi)[0] != '\0');
	XPL_TEST_CHECK(xpl_build_info_distro_version(bi) != NULL && xpl_build_info_distro_version(bi)[0] != '\0');
	XPL_TEST_CHECK(xpl_build_info_distro_codename(bi) != NULL && xpl_build_info_distro_codename(bi)[0] != '\0');
	XPL_TEST_CHECK(xpl_build_info_kernel(bi) != NULL && xpl_build_info_kernel(bi)[0] != '\0');
	XPL_TEST_CHECK(xpl_build_info_arch(bi) != NULL && xpl_build_info_arch(bi)[0] != '\0');
	XPL_TEST_CHECK(xpl_build_info_host(bi) != NULL && xpl_build_info_host(bi)[0] != '\0');
	XPL_TEST_CHECK(xpl_build_info_toolchain(bi) != NULL && xpl_build_info_toolchain(bi)[0] != '\0');
	XPL_TEST_CHECK(xpl_build_info_cc(bi) != NULL && xpl_build_info_cc(bi)[0] != '\0');
	XPL_TEST_CHECK(xpl_build_info_cc_path(bi) != NULL && xpl_build_info_cc_path(bi)[0] != '\0');
	XPL_TEST_CHECK(xpl_build_info_linker(bi) != NULL && xpl_build_info_linker(bi)[0] != '\0');
	XPL_TEST_CHECK(strcmp(xpl_build_info_linker(bi), "unknown") != 0);
}

int main(void) {
	test_version_string();
	test_version_macros_consistent();
	test_build_info();
	XPL_TEST_DONE();
}

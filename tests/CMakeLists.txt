# ============================================================================
# xplugin tests —— substrate 测试注册（对齐 xvm/cmake/tests.cmake）
#
# 每个 substrate 是独立 main()，不依赖外部框架，用 XPL_TEST_CHECK（test_util.h）。
# 回归纪律：ASan / TSan 分两趟打底（-DSAN_ADDRESS / -DSAN_THREAD）。
#
# 用法（根 CMakeLists 里 XPLUGIN_BUILD_TESTS 开启时 add_subdirectory 本目录）：
#   add_xpl_test(<target> <source.c>)
# ============================================================================

set(XPL_TEST_OUTPUT_DIR "${CMAKE_BINARY_DIR}/tests")
file(MAKE_DIRECTORY "${XPL_TEST_OUTPUT_DIR}")

function(add_xpl_test target_name source_file)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE xplugin_core)
    xpl_apply_target_defaults(${target_name})
    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${XPL_TEST_OUTPUT_DIR}"
    )
    add_test(NAME ${target_name} COMMAND ${target_name})
    # 用 GLOBAL 属性回传测试目标清单（函数内 PARENT_SCOPE 只到子目录，到不了根）
    set_property(GLOBAL APPEND PROPERTY XPLUGIN_TEST_TARGETS ${target_name})
endfunction()

# ---- 骨架冒烟 + M1a substrate（M1b 起的 substrate 逐个追加） ----
add_xpl_test(test_smoke   ${CMAKE_CURRENT_SOURCE_DIR}/test_smoke.c)
add_xpl_test(test_ctx     ${CMAKE_CURRENT_SOURCE_DIR}/test_ctx.c)
add_xpl_test(test_effect  ${CMAKE_CURRENT_SOURCE_DIR}/test_effect.c)

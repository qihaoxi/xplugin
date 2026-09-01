cmake_minimum_required(VERSION 3.16)

# ============================================================================
# xplugin —— 通用 C 微内核插件框架（C11）
#
# 常规构建目录：cmake-build-debug（见 README）
# 三档可裁剪（docs/design.md §3）：
#   core   —— 默认，仅核心库
#   loader —— -DXPLUGIN_LOADER=ON  （M3，触发门见 docs/roadmap.md §5）
#   remote —— -DXPLUGIN_REMOTE=ON  （M4，触发门见 docs/roadmap.md §6）
# 验收：cmake --build <dir> --target verify（构建 + ctest）
#       ASan / TSan 分两趟打底：-DSAN_ADDRESS=ON / -DSAN_THREAD=ON
# ============================================================================

if (DEFINED XPLUGIN_PROJECT_ROOT)
    get_filename_component(XPLUGIN_PROJECT_ROOT "${XPLUGIN_PROJECT_ROOT}" ABSOLUTE)
else ()
    set(XPLUGIN_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}")
endif ()

project(xplugin VERSION 0.1.0 LANGUAGES C)

# 嵌入友好（与 xgc/xlog「修为可嵌入」同口径）：dev-gate 全局 target（verify/format）
# 命名会与宿主工程（如 xvm 的 verify、xgc 的 format）冲突，故只在 xplugin 作为
# 顶层工程（独立 cmake）时定义；被 add_subdirectory(宿主) 嵌入时跳过。
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(XPLUGIN_TOP_LEVEL ON)
else ()
    set(XPLUGIN_TOP_LEVEL OFF)
endif ()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)  # 扩展白名单同 xvm：computed goto / _Thread_local，见 docs/design.md §10
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif ()

# ---- 选项 ----
option(XPLUGIN_BUILD_TESTS   "Build xplugin tests" ON)
option(XPLUGIN_STRICT_CHECKS "Enable stricter compiler diagnostics (-Werror etc.)" ON)
option(XPLUGIN_LOADER        "Build dlopen loader module (M3, trigger-gated)" OFF)
option(XPLUGIN_REMOTE        "Build out-of-process remote module (M4, trigger-gated)" OFF)

# ---- 通用编译/调试/sanitizer 接线 ----
include(${XPLUGIN_PROJECT_ROOT}/cmake/compiler-options.cmake)
include(${XPLUGIN_PROJECT_ROOT}/cmake/sanitizer.cmake)

# ---- 构建溯源（git commit/branch + 时间戳/主机/系统/工具链 → 生成头）----
include(${XPLUGIN_PROJECT_ROOT}/cmake/git.cmake)
include(${XPLUGIN_PROJECT_ROOT}/cmake/buildinfo.cmake)
xpl_generate_build_info(XPL_BUILD_INFO_HEADER)  # 生成 ${CMAKE_BINARY_DIR}/generated/xplugin/...

# ---- 核心库（三档的公共底座）----
add_library(xplugin_core STATIC
    src/core/build_info.c
    src/core/ctx.c
    src/core/effect.c
    src/core/version.c
)
add_library(xplugin::xplugin_core ALIAS xplugin_core)
target_include_directories(xplugin_core PUBLIC
    $<BUILD_INTERFACE:${XPLUGIN_PROJECT_ROOT}/include>
    $<INSTALL_INTERFACE:include>
)
# 生成头 xplugin_build_info_generated.h 在 include/xplugin/ 下（gitignored），
# 随 PUBLIC include 路径解析，IDE 与外部消费者所见一致
xpl_apply_target_defaults(xplugin_core)

# 触发门模块：默认 OFF，未触发不排期（docs/roadmap.md §0 触发式启用）
if (XPLUGIN_LOADER)
    target_compile_definitions(xplugin_core PUBLIC XPLUGIN_LOADER=1)
    # add_subdirectory(${XPLUGIN_PROJECT_ROOT}/src/loader ...)  ← M3 落地时挂载
endif ()
if (XPLUGIN_REMOTE)
    target_compile_definitions(xplugin_core PUBLIC XPLUGIN_REMOTE=1)
    # add_subdirectory(${XPLUGIN_PROJECT_ROOT}/src/remote ...)  ← M4 落地时挂载
endif ()

message(STATUS "xplugin version:      ${PROJECT_VERSION}")
message(STATUS "xplugin project root: ${XPLUGIN_PROJECT_ROOT}")

# ---- 测试 ----
if (XPLUGIN_BUILD_TESTS AND XPLUGIN_TOP_LEVEL)
    enable_testing()
    add_subdirectory(tests)

    get_property(XPL_TESTS GLOBAL PROPERTY XPLUGIN_TEST_TARGETS)
    add_custom_target(verify
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS xplugin_core ${XPL_TESTS}
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "xplugin: build all + run ctest"
    )
endif ()

# ---- format：cmake --build <dir> --target format（脚本模式 -P）----
find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format clang-format-18 clang-format-17 clang-format-16)
if (CLANG_FORMAT_EXECUTABLE AND XPLUGIN_TOP_LEVEL)
    add_custom_target(format
        COMMAND ${CMAKE_COMMAND}
            -D XPLUGIN_PROJECT_ROOT=${XPLUGIN_PROJECT_ROOT}
            -D CLANG_FORMAT_EXECUTABLE=${CLANG_FORMAT_EXECUTABLE}
            -P ${XPLUGIN_PROJECT_ROOT}/cmake/format.cmake
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "clang-format: format include/src/tests/examples"
    )
endif ()

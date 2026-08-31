include_guard(GLOBAL)

# 编译器诊断与输出目录约定（对齐 xvm/cmake/compiler-options.cmake）
# 纪律：零警告（-Werror//WX）、C11、`xpl_apply_target_defaults` 是唯一诊断接线口。

set(XPLUGIN_RUNTIME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin")
set(XPLUGIN_LIBRARY_OUTPUT_DIR "${CMAKE_BINARY_DIR}/lib")
set(XPLUGIN_ARCHIVE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/lib")

function(xpl_apply_target_defaults target_name)
    target_compile_features(${target_name} PUBLIC c_std_11)

    if (MSVC)
        target_compile_options(${target_name} PRIVATE
            /W4
            /permissive-
            /utf-8
            /Zc:__cplusplus
            /wd4200  # 柔性数组成员是 C99 标准；MSVC 误报为非标准扩展
            /wd4204  # 非常量聚合初始化是 C99 标准；MSVC（C 模式）误报
            /wd5105  # 老 Windows SDK winbase.h 在新编译器下的系统头警告
        )
        if (XPLUGIN_STRICT_CHECKS)
            target_compile_options(${target_name} PRIVATE /WX /sdl)
        endif ()
    else ()
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wwrite-strings
            -Wcast-align
            -Wuninitialized
            -ffile-prefix-map=${XPLUGIN_PROJECT_ROOT}/=
            -fmacro-prefix-map=${XPLUGIN_PROJECT_ROOT}/=
            -fPIC
        )
        if (XPLUGIN_STRICT_CHECKS)
            target_compile_options(${target_name} PRIVATE
                -Werror
                -Wstrict-prototypes
                -Wmissing-declarations
                -Wno-sign-compare
                -Wno-unused-parameter
                -Wno-unused-function
            )
            if (CMAKE_C_COMPILER_ID STREQUAL "GNU" AND UNIX)
                target_compile_options(${target_name} PRIVATE
                    -fstack-protector-strong
                    -fstack-clash-protection
                )
            elseif (CMAKE_C_COMPILER_ID MATCHES "Clang" AND UNIX)
                target_compile_options(${target_name} PRIVATE
                    -fstack-protector-strong
                )
            endif ()
        endif ()
    endif ()

    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${XPLUGIN_RUNTIME_OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${XPLUGIN_LIBRARY_OUTPUT_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${XPLUGIN_ARCHIVE_OUTPUT_DIR}"
    )
endfunction()

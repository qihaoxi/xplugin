# ============================================================================
# xplugin format —— clang-format 批量格式化（对齐 xvm/cmake/format.cmake）
#
# 由根 CMakeLists 的 `format` custom target 以脚本模式(-P)调用：
#   cmake --build cmake-build-debug --target format
#
# 覆盖 include / src / tests / examples；排除构建目录（cmake-build* / _deps / generated）
# ============================================================================

if (NOT DEFINED XPLUGIN_PROJECT_ROOT)
    message(FATAL_ERROR "XPLUGIN_PROJECT_ROOT is required")
endif ()

if (NOT DEFINED CLANG_FORMAT_EXECUTABLE)
    find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format clang-format-18 clang-format-17 clang-format-16)
endif ()

if (NOT CLANG_FORMAT_EXECUTABLE)
    message(FATAL_ERROR "clang-format not found. Please install clang-format to use the format target.")
endif ()

set(_xpl_format_roots
    "${XPLUGIN_PROJECT_ROOT}/include"
    "${XPLUGIN_PROJECT_ROOT}/src"
    "${XPLUGIN_PROJECT_ROOT}/tests"
    "${XPLUGIN_PROJECT_ROOT}/examples"
)

set(_xpl_format_files)
foreach(_root IN LISTS _xpl_format_roots)
    file(GLOB_RECURSE _files
        "${_root}/*.c"
        "${_root}/*.h"
    )
    list(APPEND _xpl_format_files ${_files})
endforeach()

list(REMOVE_DUPLICATES _xpl_format_files)
list(SORT _xpl_format_files)

set(_xpl_format_exclude "/cmake-build/" "/_deps/" "/generated/" "/xplugin_build_info_generated.h")
set(_xpl_format_filtered)
foreach(_file IN LISTS _xpl_format_files)
    set(_skip 0)
    foreach(_pat IN LISTS _xpl_format_exclude)
        if(_file MATCHES "${_pat}")
            set(_skip 1)
            break()
        endif()
    endforeach()
    if(NOT _skip)
        list(APPEND _xpl_format_filtered ${_file})
    endif()
endforeach()

if (_xpl_format_filtered)
    execute_process(
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" -i ${_xpl_format_filtered}
        RESULT_VARIABLE _format_result
    )
    if (NOT _format_result EQUAL 0)
        message(FATAL_ERROR "clang-format failed with exit code ${_format_result}")
    endif ()
    foreach(_file IN LISTS _xpl_format_filtered)
        message(STATUS "Formatted ${_file}")
    endforeach()
else ()
    message(STATUS "No C/C header files found to format")
endif ()

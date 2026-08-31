include_guard(GLOBAL)

# ============================================================================
# cmake/git.cmake —— 采集构建溯源信息，生成 xplugin_build_info_generated.h
#
# 范式来源：xvm/cmake/git.cmake（xvm 又从 PEL 适配），为 xplugin 扩展为完整
# provenance 集合：git commit/branch、构建时刻、主机名、操作系统（名/发行版/
# 版本/代号/内核/架构）、编译器（名+版本+路径）、链接器（名+版本）。
#
#   - 无 git / 非仓库：git 调用优雅降级为 "nogit"，绝不 FATAL。
#   - /etc/os-release 缺失或非 Linux（macOS/Windows）：发行版字段降级为
#     os 名 / "-"，绝不做平台私有探测栈（可用性优先，可读性其次）。
#   - 生成头写入构建目录（${CMAKE_BINARY_DIR}/generated/），不污染源码树、不入库。
#   - 函数式接口 xpl_generate_build_info(<out_header_var>)。
#
# 刷新：git 信息在 configure 时采集；换 commit/换机器后重跑 cmake 即更新。
# ============================================================================

function(xpl_generate_build_info out_header_var)
    # ---- git commit（无 git / 非仓库 → nogit）----
    set(_git_commit "nogit")
    set(_git_branch "nogit")
    find_program(_GIT_EXECUTABLE git)
    if (_GIT_EXECUTABLE)
        execute_process(
            COMMAND ${_GIT_EXECUTABLE} rev-parse HEAD
            WORKING_DIRECTORY ${XPLUGIN_PROJECT_ROOT}
            OUTPUT_VARIABLE _commit_raw
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _commit_rc
        )
        if (_commit_rc EQUAL 0 AND NOT "${_commit_raw}" STREQUAL "")
            # dirty 检测
            execute_process(
                COMMAND ${_GIT_EXECUTABLE} status --porcelain
                WORKING_DIRECTORY ${XPLUGIN_PROJECT_ROOT}
                OUTPUT_VARIABLE _status_raw
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if ("${_status_raw}" STREQUAL "")
                set(_git_commit "${_commit_raw}")
            else ()
                set(_git_commit "${_commit_raw}-dirty")
            endif ()

            execute_process(
                COMMAND ${_GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
                WORKING_DIRECTORY ${XPLUGIN_PROJECT_ROOT}
                OUTPUT_VARIABLE _branch_raw
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE _branch_rc
            )
            if (_branch_rc EQUAL 0 AND NOT "${_branch_raw}" STREQUAL "")
                set(_git_branch "${_branch_raw}")
            endif ()
        endif ()
    endif ()

    # ---- 时间戳 / 主机 ----
    string(TIMESTAMP _timestamp "%Y-%m-%dT%H:%M:%S")
    set(_host "unknown")
    execute_process(COMMAND hostname
        OUTPUT_VARIABLE _host_raw OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET RESULT_VARIABLE _host_rc)
    if (_host_rc EQUAL 0 AND NOT "${_host_raw}" STREQUAL "")
        set(_host "${_host_raw}")
    elseif (NOT "$ENV{HOSTNAME}" STREQUAL "")
        set(_host "$ENV{HOSTNAME}")
    endif ()

    # ---- 操作系统：名 / 发行版 / 版本 / 代号 / 内核 / 架构 ----
    set(_os "${CMAKE_SYSTEM_NAME}")
    set(_arch "${CMAKE_SYSTEM_PROCESSOR}")
    set(_distro "${_os}")
    set(_distro_version "-")
    set(_distro_codename "-")

    if (EXISTS "/etc/os-release")
        file(STRINGS "/etc/os-release" _osrel_lines REGEX "^(NAME|VERSION_ID|VERSION_CODENAME)=")
        foreach(_line IN LISTS _osrel_lines)
            if (_line MATCHES "^NAME=(.*)$")
                set(_distro "${CMAKE_MATCH_1}")
            elseif (_line MATCHES "^VERSION_ID=(.*)$")
                set(_distro_version "${CMAKE_MATCH_1}")
            elseif (_line MATCHES "^VERSION_CODENAME=(.*)$")
                set(_distro_codename "${CMAKE_MATCH_1}")
            endif ()
        endforeach()
        # 去掉 os-release 的 shell 双引号
        string(REGEX REPLACE "^\"(.*)\"$" "\\1" _distro "${_distro}")
        string(REGEX REPLACE "^\"(.*)\"$" "\\1" _distro_version "${_distro_version}")
        string(REGEX REPLACE "^\"(.*)\"$" "\\1" _distro_codename "${_distro_codename}")
    endif ()

    set(_kernel "-")
    if (UNIX)
        execute_process(COMMAND uname -r
            OUTPUT_VARIABLE _kernel_raw OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET RESULT_VARIABLE _kernel_rc)
        if (_kernel_rc EQUAL 0 AND NOT "${_kernel_raw}" STREQUAL "")
            set(_kernel "${_kernel_raw}")
        endif ()
    elseif (WIN32)
        set(_kernel "${CMAKE_SYSTEM_VERSION}")
    endif ()

    # ---- 编译器：名+版本 / 路径 ----
    set(_compiler "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
    set(_compiler_path "${CMAKE_C_COMPILER}")

    # ---- 链接器：名+版本（CMAKE_LINKER 优先，POSIX 回退 ld/lld）----
    set(_linker "unknown")
    set(_linker_prog "")
    if (CMAKE_LINKER)
        set(_linker_prog "${CMAKE_LINKER}")
    endif ()
    if (NOT _linker_prog AND UNIX)
        find_program(_XPL_LD NAMES ld.bfd ld ld.lld lld)
        set(_linker_prog "${_XPL_LD}")
    endif ()
    if (_linker_prog)
        execute_process(COMMAND "${_linker_prog}" --version
            OUTPUT_VARIABLE _linkerv OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        # 第一行通常是链接器描述，如 "GNU ld (GNU Binutils for Debian) 2.44"
        string(REGEX MATCH "^[^\r\n]+" _linker_line "${_linkerv}")
        if (NOT "${_linker_line}" STREQUAL "")
            set(_linker "${_linker_line}")
        endif ()
    elseif (MSVC)
        set(_linker "link.exe (MSVC toolset)")
    endif ()

    # ---- 转义（防注入换行/引号）----
    foreach (_v _git_commit _git_branch _timestamp _host _os _distro _distro_version
              _distro_codename _kernel _arch _compiler _compiler_path _linker)
        string(REPLACE "\\" "\\\\" ${_v} "${${_v}}")
        string(REPLACE "\"" "\\\"" ${_v} "${${_v}}")
        string(REPLACE "\n" " " ${_v} "${${_v}}")
    endforeach ()

    # ---- 生成头（构建目录内）----
    set(_gen_dir "${CMAKE_BINARY_DIR}/generated/xplugin")
    set(_gen_header "${_gen_dir}/xplugin_build_info_generated.h")
    file(MAKE_DIRECTORY "${_gen_dir}")
    file(WRITE  "${_gen_header}" "/* auto-generated by cmake/git.cmake — do not edit, not tracked */\n")
    file(APPEND "${_gen_header}" "#ifndef XPLUGIN_BUILD_INFO_GENERATED_H\n#define XPLUGIN_BUILD_INFO_GENERATED_H\n\n")
    file(APPEND "${_gen_header}" "#define XPL_BUILD_GIT_COMMIT       \"${_git_commit}\"\n")
    file(APPEND "${_gen_header}" "#define XPL_BUILD_GIT_BRANCH       \"${_git_branch}\"\n")
    file(APPEND "${_gen_header}" "#define XPL_BUILD_TIMESTAMP        \"${_timestamp}\"\n")
    file(APPEND "${_gen_header}" "#define XPL_BUILD_HOST             \"${_host}\"\n")
    file(APPEND "${_gen_header}" "#define XPL_BUILD_OS               \"${_os}\"\n")
    file(APPEND "${_gen_header}" "#define XPL_BUILD_DISTRO           \"${_distro}\"\n")
    file(APPEND "${_gen_header}" "#define XPL_BUILD_DISTRO_VERSION   \"${_distro_version}\"\n")
    file(APPEND "${_gen_header}" "#define XPL_BUILD_DISTRO_CODENAME  \"${_distro_codename}\"\n")
    file(APPEND "${_gen_header}" "#define XPL_BUILD_KERNEL           \"${_kernel}\"\n")
    file(APPEND "${_gen_header}" "#define XPL_BUILD_ARCH             \"${_arch}\"\n")
    file(APPEND "${_gen_header}" "#define XPL_BUILD_COMPILER         \"${_compiler}\"\n")
    file(APPEND "${_gen_header}" "#define XPL_BUILD_COMPILER_PATH    \"${_compiler_path}\"\n")
    file(APPEND "${_gen_header}" "#define XPL_BUILD_LINKER           \"${_linker}\"\n\n")
    file(APPEND "${_gen_header}" "#endif /* XPLUGIN_BUILD_INFO_GENERATED_H */\n")

    message(STATUS "build-info: commit=${_git_commit} branch=${_git_branch} @ ${_timestamp}")
    message(STATUS "build-info: os=${_distro} ${_distro_version} (${_distro_codename}) kernel=${_kernel} ${_arch}")
    message(STATUS "build-info: cc=${_compiler} ld=${_linker}")
    set(${out_header_var} "${_gen_header}" PARENT_SCOPE)
endfunction()

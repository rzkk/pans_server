# 以 CMake 脚本模式执行单个第三方库步骤（configure / build / install）。
#
# 成功：把 stdout/stderr 写入日志文件，只输出一行进度提示。
# 失败：显示日志末尾并让构建立即停止，完整日志仍保留。
#
# 推荐调用方式（由 pansDependencies.cmake 生成）：
#   cmake -DLOG_FILE=<路径> -DLOG_TITLE=<标题> -P runDependencyStep.cmake -- <命令> <参数...>
#
# 使用 -- 直接传递命令及每个参数，避免把 CMake 列表塞入 -DCMD 后
# 又被 COMMAND_EXPAND_LISTS 拆开。旧的 -DCMD=cmd;arg1;arg2 调用仍兼容。

if(NOT DEFINED LOG_FILE OR "${LOG_FILE}" STREQUAL "")
    message(FATAL_ERROR "runDependencyStep: LOG_FILE is required")
endif()

if(NOT DEFINED LOG_TITLE OR "${LOG_TITLE}" STREQUAL "")
    set(LOG_TITLE "dependency")
endif()

# 失败时显示的日志行数，可用 -DLOG_TAIL_LINES=... 覆盖。
if(NOT DEFINED LOG_TAIL_LINES OR "${LOG_TAIL_LINES}" STREQUAL "")
    set(LOG_TAIL_LINES 120)
endif()
if(NOT "${LOG_TAIL_LINES}" MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "runDependencyStep: LOG_TAIL_LINES must be a positive integer")
endif()

# 在 -P 脚本模式中，命令行参数可通过 CMAKE_ARGC/CMAKE_ARGVn 读取。
# 找到 -- 后，后面的参数就是要执行的程序及其参数。
set(_command)
set(_after_separator FALSE)
if(CMAKE_ARGC GREATER 0)
    math(EXPR _last_index "${CMAKE_ARGC} - 1")
    foreach(_index RANGE 0 ${_last_index})
        set(_argument "${CMAKE_ARGV${_index}}")
        if(_after_separator)
            # CMake 列表使用分号分隔；保护参数自身包含的分号。
            string(REPLACE ";" "\\;" _argument "${_argument}")
            list(APPEND _command "${_argument}")
        elseif(_argument STREQUAL "--")
            set(_after_separator TRUE)
        endif()
    endforeach()
endif()

list(LENGTH _command _command_length)
if(_after_separator)
    if(_command_length EQUAL 0)
        message(FATAL_ERROR "runDependencyStep: a command is required after --")
    endif()
elseif(DEFINED CMD AND NOT "${CMD}" STREQUAL "")
    # 兼容原先的 -DCMD=cmd;arg1;arg2 接口。
    set(_command ${CMD})
else()
    message(FATAL_ERROR "runDependencyStep: command is required (use -- <command> <args...>)")
endif()

# 日志目录可能还不存在；相对日志文件名无需创建目录。
get_filename_component(_log_dir "${LOG_FILE}" DIRECTORY)
if(NOT "${_log_dir}" STREQUAL "")
    file(MAKE_DIRECTORY "${_log_dir}")
endif()

# OUTPUT_FILE 与 ERROR_FILE 相同时，标准输出、标准错误会合并写入。
execute_process(
    COMMAND ${_command}
    RESULT_VARIABLE _result
    OUTPUT_FILE "${LOG_FILE}"
    ERROR_FILE  "${LOG_FILE}"
)

if(NOT _result STREQUAL "0")
    # 保留日志中的分号，不用 string(REPLACE ";" "\n" ...) 拼接行。
    file(STRINGS "${LOG_FILE}" _log_lines)
    list(LENGTH _log_lines _line_count)
    if(_line_count GREATER LOG_TAIL_LINES)
        math(EXPR _tail_start "${_line_count} - ${LOG_TAIL_LINES}")
        list(SUBLIST _log_lines ${_tail_start} -1 _log_lines)
    endif()

    set(_tail_text "")
    foreach(_log_line IN LISTS _log_lines)
        string(APPEND _tail_text "${_log_line}\n")
    endforeach()

    message(FATAL_ERROR
        "\n"
        "第三方库步骤失败：${LOG_TITLE}\n"
        "返回码：${_result}\n"
        "完整日志：${LOG_FILE}\n"
        "---------------- 日志尾部（最多 ${LOG_TAIL_LINES} 行） ----------------\n"
        "${_tail_text}"
        "------------------------------------------------------------------\n"
    )
endif()

# 成功时只保留简短提示。
message(STATUS "${LOG_TITLE}: done (log: ${LOG_FILE})")

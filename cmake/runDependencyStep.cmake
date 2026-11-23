# 以脚本模式执行一条第三方库构建命令（configure / build / install）。
#
# 目的：
#   1. 第三方库的 configure/build/install 输出通常非常冗长，全部写入日志文件；
#   2. 只有命令失败时，才把日志尾部打印到终端，并让构建立即失败。
#
# 调用方式（由 cmake/pansDependencies.cmake 生成）：
#   cmake -DLOG_FILE=<日志路径> -DLOG_TITLE=<步骤标题> -DCMD=<命令列表> -P runDependencyStep.cmake
#
# 注意：CMD 是一个 CMake 列表（分号分隔），在命令行上必须整体加引号传入。

if(NOT DEFINED LOG_FILE OR "${LOG_FILE}" STREQUAL "")
    message(FATAL_ERROR "runDependencyStep: LOG_FILE is required")
endif()
# if(NOT DEFINED CMD OR "${CMD}" STREQUAL "")
#     message(FATAL_ERROR "runDependencyStep: CMD is required")
# endif()

# 失败时回显的日志行数，可通过 -DLOG_TAIL_LINES=... 覆盖
if(NOT DEFINED LOG_TAIL_LINES OR "${LOG_TAIL_LINES}" STREQUAL "")
    set(LOG_TAIL_LINES 120)
endif()

# 日志目录可能还不存在
get_filename_component(_log_dir "${LOG_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_log_dir}")

# OUTPUT_FILE 与 ERROR_FILE 指向同一文件时，CMake 会把 stdout/stderr 合并写入。
# 注意：该文件会被截断（每次步骤独立一个日志文件，所以不会丢内容）。
execute_process(
    COMMAND ${CMD}
    RESULT_VARIABLE _result
    OUTPUT_FILE "${LOG_FILE}"
    ERROR_FILE  "${LOG_FILE}"
)

if(NOT _result STREQUAL "0")
    # 只打印日志尾部，避免刷屏；完整内容保留在日志文件中
    file(STRINGS "${LOG_FILE}" _log_lines)
    list(LENGTH _log_lines _line_count)
    if(_line_count GREATER LOG_TAIL_LINES)
        math(EXPR _tail_start "${_line_count} - ${LOG_TAIL_LINES}")
        list(SUBLIST _log_lines ${_tail_start} -1 _log_lines)
    endif()
    string(REPLACE ";" "\n" _tail_text "${_log_lines}")

    message(FATAL_ERROR
        "\n"
        "第三方库步骤失败：${LOG_TITLE}\n"
        "返回码：${_result}\n"
        "完整日志：${LOG_FILE}\n"
        "---------------- 日志尾部（最后 ${LOG_TAIL_LINES} 行） ----------------\n"
        "${_tail_text}\n"
        "------------------------------------------------------------------\n"
    )
endif()

# 成功时只留一行进度提示，避免刷屏
message(STATUS "${LOG_TITLE}: done (log: ${LOG_FILE})")
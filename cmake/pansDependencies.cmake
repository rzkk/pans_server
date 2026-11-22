# 本文件的目标：把 third_party 中的源码分别配置、编译并安装到一个缓存目录，
# 再让 pans 使用这些安装结果。它刻意不使用 add_subdirectory(third_party/xxx)，
# 这样第三方库的选项、目标名和编译设置不会污染 pans 自己的 CMake 工程。
#
# 阅读时先区分 CMake 的两个阶段：
# 1. 配置阶段：执行本文件中的 set()/if()/function()/add_custom_target() 等命令，
#    生成底层构建系统（例如 Makefile 或 Ninja 文件）。
# 2. 构建阶段：真正执行 add_custom_command() 中登记的“配置第三方库、编译、安装”命令。
# execute_process() 是例外，它会在配置阶段立即执行。
#
# build_commands 还要求 cmake/writeDependencyStamp.cmake 存在，kcp 包装构建还要求
# cmake/kcp/CMakeLists.txt 存在。缺少这些接入条件时，本文件的设计尚不能完整运行。


# 第三方依赖的源码根目录。
#set(变量名 变量值 CACHE 缓存类型 "描述信息")
#   ${PROJECT_SOURCE_DIR} ： 当前 project() 对应的项目源码根目录
#使用 CACHE 变量后，可以在 CMake 配置时通过,CACHE PATH 表示它会写入canche缓存 CMakeCache.txt 中，
#用户可以在首次配置时用 -DPANS_THIRD_PARTY_DIR=/some/path 覆盖默认值，之后会沿用缓存值。
#普通变量用于脚本内部计算；缓存变量适合需要由用户配置、并且在多次配置间保留的选项
#几个常见的缓存变量类型：类型	        用途
#                       BOOL	    开关
#                       PATH	    目录路径
#                       FILEPATH	文件路径
#                       STRING	    普通字符串

set(PANS_THIRD_PARTY_DIR  "${PROJECT_SOURCE_DIR}/third_party" CACHE PATH "Path to the third party directory")
#PANS_THIRD_PARTY_OUTPUT_DIR : 编译产物根目录，默认 third_party/output
set(PANS_THIRD_PARTY_OUTPUT_DIR  "${PANS_THIRD_PARTY_DIR}/output" CACHE PATH "compiled third-party output")
set(PANS_THIRD_PARTY_BUILD_TYPE  Release CACHE STRING "third-party build type")


#语法 ：  option(变量名 "说明" 默认值)
# 可以使用 -DPANS_UPDATE_SUBMODULES=OFF 覆盖默认值，进行关闭。
#含义：  是否在依赖缺失时初始化或更新 Git 子模块。 源码缺失时，是否尝试通过 Git 初始化子模块 , 是否下载缺失的第三方库
option(PANS_UPDATE_SUBMODULES "Update Git submodules if they are missing" ON)

# 是否优先复用 third_party/output 中与当前配置兼容的已编译依赖。 是否复用已编译产物
# 关闭后通常会强制走第三方依赖的重新构建流程。
option(PANS_REUSE_INSTALLED_DEPENDENCIES "Reuse compatible dependencies from third_party/output" ON)

#流程:foreach 拆分 → 检查哨兵文件 → 收集缺失项 → 若缺失则 git submodule update --init --recursive -- <paths> → 再校验一次。
# CMake 没有真正独立的数组类型，通常使用分号分隔的字符串表示列表：子模块完整性检查清单。
# 每一项使用“子模块目录|哨兵文件”格式：当哨兵文件不存在时，说明对应子模块可能未初始化或内容不完整，后续配置逻辑可据此决定是否同步子模块。
# 这里选择稳定且能代表源码已检出的文件，而不是依赖目录本身是否存在。
set(_pans_required_submodules 
    "third_party/yaml-cpp|CMakeLists.txt"
    "third_party/abseil-cpp|CMakeLists.txt"
    "third_party/protobuf|CMakeLists.txt"
    "third_party/rapidjson|include/rapidjson/document.h"
    "third_party/tinyxml2|CMakeLists.txt"
    "third_party/kcp|ikcp.c"               
    "third_party/zlib|CMakeLists.txt"
)

# 查找系统的 Git 可执行程序。只有在需要初始化或更新子模块时，后续逻辑才需要使用它；
# 得到Git_FOUND ， GIT_EXECUTABLE
# QUIET 表示查找失败时不要输出普通提示。 Git_FOUND 为 FALSE 
# REQUIRED ：立即 FATAL_ERROR，配置中止
find_package(Git  QUIET)
# find_package(Git  REQUIRED)

# 函数格式 ： 
#     function(function_name parameter1 parameter2)
#     ...
#       endfunction()
# notes: list 构建
# 方式 1：多元素直接定义（底层等价于 "item1;item2;item3"）
# set(_pans_required_submodules
#     "third_party/yaml-cpp|CMakeLists.txt"
#     "third_party/abseil-cpp|CMakeLists.txt"
# )
# 方式 2：通过 string(REPLACE) 将竖线替换为分号，直接生成含 2 个元素的列表 parts
#   string(REPLACE "|" ";" parts "${entry}") 

# 更新子模块 ， 同步子模块代码
function(pans_init_third_party_submodules)
# function() 会创建局部变量作用域；这里不会污染调用者的 missing_submodules。
    set(missing_submodules) #设置缺失的库 ，  刚开始为空
    
    # 收集哪些子模块缺失或内容不完整
    # IN LISTS 后面写的是“变量名”，不是 ${变量值}。
    foreach(enrty IN LISTS _pans_required_submodules)
        #语法：string(REPLACE 原字符串 替换字符串 结果变量 输入字符串)
        # "a|b" -> "a;b"；分号让 parts 成为含两个元素的 CMake 列表。
        string(REPLACE "|" ";" parts "${entry}")
        #list(GET 列表 下标 输出变量) ，下标从 0 开始。
        list(GET parts 0 submodule_path)
        list(GET parts 1 sentinel)

        # 如果路径不存在 // if(VAR) if(NOT VAR)  if(VAR STREQUAL "Linux") if(EXISTS "/tmp/file") if(TARGET some_target)
        if(NOT EXISTS "${PROJECT_SOURCE_DIR}/${submodule_path}/${sentinel}")
            list(APPEND missing_submodules "${submodule_path}")
        endif()
    endforeach()

    # 如果不存在缺失的子模块，则提示用户并返回
    if(NOT missing_submodules)
        message(STATUS "All required submodules are present.")
        return()
    endif()

    # 如果存在缺失的子模块，则提示用户并尝试初始化或更新子模块
    if(NOT PANS_UPDATE_SUBMODULES)
        message(FATAL_ERROR "Some required submodules are missing but PANS_UPDATE_SUBMODULES is OFF.")
    endif()

    # 网站压缩包下载的 就没有这个文件夹 ， 不能更新    # exists
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/.git")
            message(FATAL_ERROR
                "Missing third-party source trees: ${missing_submodules}\n"
                "The source tree is not a git checkout, so CMake cannot initialize them.")    
    endif()

    if(NOT Git_FOUND)  # 没有找到git 工具
        message(FATAL_ERROR "Git is required but not found.")
    endif()
    message(STATUS "Initializing missing git submodules: ${missing_submodules}")

    #在 CMake 配置阶段执行外部程序。等价于： 
    # git submodule update --init --recursive -- third_party/yaml-cpp  third_party/protobuf
    execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive -- ${missing_submodules}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} #命令执行目录
        RESULT_VARIABLE submodule_result       # 保存进程退出码     0 成功 ， 非 0  失败
        OUTPUT_VARIABLE submodule_output       # 捕获标准输出
        ERROR_VARIABLE  submodule_error        # 捕获标准错误
    )

    if(NOT submodule_result STREQUAL "0")  #接着检查退出码
        message(FATAL_ERROR "Failed to initialize missing git submodules: ${missing_submodules}")
    endif()


    #再次检查
    foreach(enrty IN LISTS _pans_required_submodules)
      
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 submodule_path)
        list(GET parts 1 sentinel)

        if(NOT EXISTS "${PROJECT_SOURCE_DIR}/${submodule_path}/${sentinel}")
            message(FATAL_ERROR "Required third-party source is still missing: "
                "${submodule_path}/${sentinel}")
        endif()
    endforeach()

endfunction()

pans_init_third_party_submodules() #执行一下函数


# 如何知道之前编译过的二进制库，还能不能继续使用？ 最基本的条件是：源码版本没有变化。
# 获取子模块commit ID的函数 -> 用于判断子模块是否被更新过
# 取得依赖源码版本，作为后面缓存键的一部分。
# 源码版本变化时，旧二进制缓存必须失效。 
#revision : 修改版 修订版 ， 版本
function(pans_get_source_revision  source_dir output_variable)
    if(Git_FOUND AND EXISTS "${source_dir}/.git") #有 git 时用 commit SHA 作为版本标识。
        execute_process(   # executable
            #git -C third_party/yaml-cpp rev-parse HEAD 
            # -C：让 Git 在指定目录下操作。 
            # rev-parse HEAD：取得当前 HEAD 指向的提交 ID。
            COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" rev-parse HEAD
            RESULT_VARIABLE revision_result
            OUTPUT_VARIABLE revision
            ERROR_VARIABLE  revision_error
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT revision_result STREQUAL "0") #0 是成功
            message(FATAL_ERROR "Failed to get source revision: ${source_dir}: ${revision_error}")
        endif()
    else()
        #计算指定文件内容的 SHA256 哈希值。 当文件内容改变时，哈希值通常也会发生改变
        file(SHA256 "${source_dir}/CMakeLists.txt" revision) #无 git 时退化为 CMakeLists.txt 的 SHA256。
    endif()
    set(${output_variable} ${revision} PARENT_SCOPE)
    # PARENT_SCOPE ： 将变量设置到父作用域，而不是当前函数作用域。 
endfunction()

#给它一个第三方库的信息，它就自动帮你创建配置、编译、安装、缓存和依赖管理任务。
#给子模块生成一个构建规则
function(pans_add_cmake_dependency)

    
endfunction()

set(_pans_static_library "${CMAKE_STATIC_LIBRARY_PREFIX}@NAME@${CMAKE_STATIC_LIBRARY_SUFFIX}")

# -------- abseil-cpp --------
set(_pans_abseil_source "${PANS_THIRD_PARTY_DIR}/abseil-cpp")
pans_get_source_revision("${_pans_abseil_source}" _pans_abseil_revision)

string(REPLACE "@NAME@" "abseil_base" _pans_abseil_library "{_pans_static_library}" )

message(STATUS "${_pans_static_library}  ,${CMAKE_STATIC_LIBRARY_PREFIX},  ${CMAKE_STATIC_LIBRARY_SUFFIX}   ")
message(STATUS "${_pans_static_library} ")


find_package(Threads REQUIRED)  #这是给 pans 用的 

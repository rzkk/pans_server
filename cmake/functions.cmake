# 让 C++ 程序中的 __FILE__ 宏输出项目相对路径，而不是冗长的绝对路径。
#
# 【为什么不再用 -D__FILE__=...】
#   __FILE__ 是编译器内建宏，用 -D 重新定义会触发：
#       <command-line>: warning: "__FILE__" redefined [-Wbuiltin-macro-redefined]
#   而且 -D 是作用于整个编译单元（TU）的，头文件里的 __FILE__ 也会被替换成
#   那个 .cpp 的路径，日志里定位会出错。
#
# 【推荐做法】使用编译器的 -fmacro-prefix-map（GCC 8+ / Clang 10+）
#   它只影响 __FILE__ / __BASE_FILE__ 的展开结果，不重定义内建宏，
#   因此没有警告，且每个文件仍然报告自己的真实路径。
#
# 用法：use_relative_file_macro(pans)
# -fmacro-prefix-map 是对target的 编译选项
function(use_relative_file_macro target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # 把 /path/to/project/ 前缀映射为空，__FILE__ 就变成 pans/src/xxx.cpp
        target_compile_options(${target} PRIVATE
            # "-fmacro-prefix-map=${PROJECT_SOURCE_DIR}/="
            "-ffile-prefix-map=${PROJECT_SOURCE_DIR}/="
        )
    elseif(MSVC)
        # MSVC 没有等价选项，只能裁剪调试信息里的路径
        target_compile_options(${target} PRIVATE
            "/d1trimfile:${PROJECT_SOURCE_DIR}/"
        )
    endif()
endfunction()

# 会有 宏重定义警告
function(redefine_file_macro targets)
    foreach(source_file IN LISTS targets)
        if("${source_file}" STREQUAL "")
            continue()
        endif()
        get_filename_component(absolute_path "${source_file}" ABSOLUTE)
        file(RELATIVE_PATH relative_path "${PROJECT_SOURCE_DIR}" "${absolute_path}")
        # 项目目录外的生成文件不保留其绝对路径。
        if(IS_ABSOLUTE "${relative_path}" OR relative_path STREQUAL ".." OR relative_path MATCHES "^\\.\\./")
            get_filename_component(relative_path "${source_file}" NAME)
        endif()
        set_source_files_properties("${source_file}" PROPERTIES COMPILE_DEFINITIONS "__FILE__=\"${relative_path}\"")
    endforeach()
endfunction()

# 让 C++ 程序中的 __FILE__ 宏输出项目相对路径，而不是冗长的绝对路径。
# 两种遍历 
    # 写法一：遍历 FILES 变量的内容
    # foreach(file IN LISTS FILES)
    #     message(STATUS "${file}")
    # endforeach()

    # # 写法二：直接提供列表中的元素
    # foreach(file IN ITEMS ${FILES})
    #     message(STATUS "${file}")
    # endforeach()
# get_filename_component(输出变量  输入路径 操作模式 )
    # 操作模式
        # ABSOLUTE	获取绝对路径	    /home/rzk/pans/src/main.cpp
        # NAME	    只获取文件名	    main.cpp
        # DIRECTORY	获取所在目录	    /home/rzk/pans/src
        # EXT	    获取扩展名	                .cpp
        # NAME_WE	获取不带扩展名的文件名	    main
        # REALPATH	获取绝对路径，并解析符号链接	实际文件路径
    # 注：传入的相对路径 ，ABSOLUTE 默认以 CMAKE_CURRENT_SOURCE_DIR 为基准 ，
            #但也可以手动指定  BASE_DIR "${PROJECT_SOURCE_DIR}"
#file(RELATIVE_PATH  输出变量    基准目录    目标文件)   ： 计算从基准目录出发，如何到达目标文件。
    # 如 ：PROJECT_SOURCE_DIR = /home/rzk/pans_rzk  ，absolute_path = /home/rzk/pans_rzk/src/main.cpp
    # file(RELATIVE_PATH relative_path "${PROJECT_SOURCE_DIR}" "${absolute_path}")
    # 得到 ： relative_path = "src/main.cpp" 
    # 这相当于把项目根目录前面的部分去掉，但不是简单的字符串替换，而是真正计算文件系统路径之间的相对关系。
    # PROJECT_SOURCE_DIR	最近一次适用的 project() 所定义项目的源目录
    # CMAKE_CURRENT_SOURCE_DIR	当前正在处理的 CMakeLists.txt 所在源目录


#if(IS_ABSOLUTE "${relative_path}" OR relative_path STREQUAL ".." OR relative_path MATCHES "^\\.\\./")
        #条件一：IS_ABSOLUTE "${relative_path}" 
            # 检查 relative_path 是否为绝对路径，如果是，则说明文件不在项目源目录下。
            # windows 下，不同盘下面的文件 ， 无法通过 ../ 形式表示跨盘符相对路径
        #条件二：relative_path STREQUAL ".." 
            # 检查 relative_path 是否为上级目录，若是，则说明文件不在项目源目录下。
        #条件三：relative_path MATCHES "^\\.\\./"       
            # 检查 relative_path 是否以 "../" 开头，若是，则说明文件不在项目源目录下。
            # MATCHES 表示使用正则表达式进行匹配。

#set_source_files_properties(源文件 PROPERTIES 属性名 属性值)     
    #     它能够给某个源文件设置编译属性。
    # 如：set_source_files_properties(src/main.cpp PROPERTIES COMPILE_DEFINITIONS "MY_DEBUG=1")
    # 相当于在编译 main.cpp 时添加类似： -DMY_DEBUG=1 的编译选项。
    # COMPILE_DEFINITIONS ： 可能覆盖原有属性 
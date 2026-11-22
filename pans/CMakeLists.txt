file(GLOB_RECURSE PANS_SOURCES CONFIGURE_DEPENDS 
#这种写法可以包含目录下所有的文件， 包括文件夹下的
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
# *.cc  
# *.c
)

# 前面两个是暴露给外部的
# 后面两个是内部的头文件
file(GLOB_RECURSE PANS_HEADERS CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)


add_library(pans STATIC ${PANS_SOURCES} ${PANS_HEADERS}) # 生成 libpans.a


# 让 __FILE__ 输出项目相对路径（使用 -fmacro-prefix-map，不会触发重定义警告）
use_relative_file_macro(pans) #

# 告诉使用 pans 的其他 CMake 目标至少需要 C++20
target_compile_features(pans PUBLIC cxx_std_20 )

# 允许静态库将来链接到共享库中 # 取代-fPIC
# 这行设置让 pans 这个静态库在编译时加上 -fPIC（Position Independent Code，位置无关代码）
set_target_properties(pans PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# $<INSTALL_INTERFACE:include> ：别人在使用安装后的 pans 库时，头文件的路径是 include/xxx.h 
# 这里必须是 相对路径写法
# 表示安装后的使用者，从安装前缀下的 include 目录查找头文件。方便 find_package
# BUILD_INTERFACE：  确保构建树中的依赖者可以直接使用 target_link_libraries(demo PRIVATE pans)
#                       获得源码目录中的头文件路径。此时头文件还在源码目录
target_include_directories(pans 
    PUBLIC      # pans 公开的头文件
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>  
        $<INSTALL_INTERFACE:include>  #被别人使用时，使用的头文件路径 , 相对于 CMAKE_INSTALL_PREFIX的
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# message(STATUS "--------${CMAKE_INSTALL_INCLUDEDIR}")
# CMAKE_INSTALL_INCLUDEDIR : 的值就是include 



# 使用 public  ， 本身就是为了将这份配置与pans库组合打包给用户用的
target_link_libraries(pans PUBLIC   pans_options)    
target_link_libraries(pans PUBLIC Threads::Threads)

# 用来查看函数堆栈的
if(WIN32)
    target_link_libraries(pans PRIVATE dbghelp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(pans PRIVATE PkgConfig::PANS_LIBDW)
endif()

target_link_libraries(pans PUBLIC yaml-cpp::yaml-cpp)

add_dependencies(pans pans_third_party_dependencies)

# 主库可能需要更严格的编译选项
target_compile_options(pans PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
        -Werror          # 将警告视为错误（主库严格要求）
    >
) 




## 库的安装配置
#install()：定义安装规则，控制编译产物和头文件最终安装到哪里。
# 下面三个变量由 include(GNUInstallDirs) 提供
# ${CMAKE_INSTALL_LIBDIR} 是 CMake 中定义的库安装目录变量。  # 通常是 lib，某些系统为 lib64
#"${CMAKE_INSTALL_INCLUDEDIR}" # 通常是 include
install(TARGETS pans      #TARGETS 表示安装 CMake 中已经定义好的目标 ,这里的 pans 是 CMake 目标名称
    EXPORT pansTargets    #将 pans 目标加入名为 pansTargets 的导出集合，方便其他 CMake 项目导入和使用。
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"  # ARCHIVE 通常指静态库，以及 Windows DLL 对应的导入库。 DESTINATION 表示安装位置。
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"  # LIBRARY 主要对应 Unix/Linux 下的动态共享库，以及 CMake 的模块库。
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"  # RUNTIME 对应可执行程序，以及 Windows 下的 DLL 动态库。
)
# 安装公开头文件，保留 logger 等目录层级  
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"  #保留层级， DIRECTORY 表示安装一个目录及其符合规则的内容。
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING            #只安装匹配后续 PATTERN 或 REGEX 规则的文件。
        PATTERN "*.h"
        PATTERN "*.hpp"
)
# DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include" ： 表示要安装的目录是 ${CMAKE_CURRENT_SOURCE_DIR}/include
    # include后面无 / ： 表示CMake 会将 include 目录本身复制到 /usr/local/include 中。
    # include后面有 / ： 表示CMake 会将 include 目录下的内容复制到 /usr/local/include 中，而不会创建 include 目录本身。
# DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" ： 表示安装的目标目录是 ${CMAKE_INSTALL_INCLUDEDIR}，
                                                # 这个变量通常指向系统的 include 目录。


# 还需要安装 pans_options ： INTERFACE 库和依赖传递。
    #负责存储和传递编译需求的。 
install(TARGETS pans_options EXPORT pansTargets)



# notes： 
# install 
    # 形式 ： 
        #TARGETS ：  安装 CMake Target pans 的产物和相关信息
        #FILES  ： 安装一个指定文件
        #DIRECTORY ： 安装一个指定目录及其内容
    # EXPORT pansTargets ：
        # pansTargets 是一个**导出集合（Export Set）**的名字。  自己定义的 , 一个将来需要导出的目标清单。
        # 导出集合是一个 CMake 概念，用于将一组 CMake 目标（Targets）打包在一起，以便其他 CMake 项目可以轻松地导入和使用这些目标。
        # 顶层的CMakeLists.txt 中的 install(EXPORT ...) 命令使用
        #install(TARGETS ... EXPORT ...) 是登记导出目标，而 install(EXPORT ...) 才是生成并安装导出文件。
    # CMAKE_INSTALL_PREFIX ： 它决定安装目录的根路径。
        # 如 ： cmake --install build --prefix /home/rzk/pans_install ->  CMAKE_INSTALL_PREFIX 为 /home/rzk/pans_install
        # 实际安装目录 ：  CMAKE_INSTALL_PREFIX /  CMAKE_INSTALL_LIBDIR 


    


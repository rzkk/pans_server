cmake_minimum_required(VERSION 3.25)

#CMAKE_HOST_SYSTEM_NAME : CMake 正在其上运行的 OS 的名称。
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_C_COMPILER "/usr/bin/gcc-13" CACHE FILEPATH "C compiler" FORCE)
    set(CMAKE_CXX_COMPILER "/usr/bin/g++-13" CACHE FILEPATH "C++ compiler" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(pans VERSION 0.1.0 LANGUAGES CXX  C )

# message(STATUS ${CMAKE_HOST_SYSTEM_NAME})

#todo 为了install
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(PANS_BUILD_EXAMPLES "Build the examples" ON)
option(PANS_BUILD_TEST "Build tests" ON)         # 是否编译测试程序
option(PANS_BUILD_TOOLS "Build tools " OFF)      # 是否编译工具程序

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

# set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")


# 统一目标输出目录
# CMAKE_BINARY_DIR :
# 静态库：libpans.a
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/lib")
# 动态库：libpans.so
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/lib")
# 可执行程序：测试程序和工具程序
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
#PROJECT_SOURCE_DIR 与 CMAKE_CURRENT_SOURCE_DIR 
# 根目录下的 ./lib 

# message(STATUS ${PROJECT_SOURCE_DIR})

# 引入cmake/... 配置 
include(cmake/functions.cmake)
include(cmake/pansOptions.cmake)
include(cmake/pansDependencies.cmake)

add_subdirectory(pans)
if(PANS_BUILD_TEST)
    add_subdirectory(tests)
endif()

if(PANS_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

#安装规则 
set(PANS_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/pans")
#CMAKE_INSTALL_LIBDIR   #lib  相对安装目录前缀的


# 生成配置文件
# 告诉其他项目如何使用 pans , 生成pansConfig.cmake
# notes ： configure_package_config_file(
                #     <input>
                #     <output>
                #     INSTALL_DESTINATION <path>   #生成的配置文件未来计划安装到哪个目录。
                #     [PATH_VARS <var1> <var2> ...]
                # )
            #  由 include(CMakePackageConfigHelpers) 提供
#CMAKE_CURRENT_BINARY_DIR ：是cmake --build 的目录 
configure_package_config_file(
    cmake/pansConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/pansConfig.cmake"
    INSTALL_DESTINATION "${PANS_INSTALL_CMAKEDIR}"  
)

#告诉其他项目 pans 的版本是否满足要求 ， 生成pansConfigVersion.cmake
# write_basic_package_version_file(
#     <filename>                          # 输出文件路径（必填）
#     [VERSION <major.minor.patch>]       # 省略则用 PROJECT_VERSION
#     COMPATIBILITY <mode>                # 必填，四选一
#     [ARCH_INDEPENDENT]                  # 跳过 32/64 位检查
# )
# 这个文件是在 configure 阶段生成的（不是 install 阶段) 下游执行 find_package(pans 0.1 REQUIRED) 时，
# CMake 会：找到 pansConfig.cmake；发现同目录有 pansConfigVersion.cmake，先执行它；
# 执行前注入 PACKAGE_FIND_VERSION = "0.1"、PACKAGE_FIND_VERSION_MAJOR = "0" 等变量；
# 执行后读取它设置的结论变量。
# COMPATIBILITY <mode> 
    # AnyNewerVersion	已安装版本 ≥ 请求版本	接口只增不改，最宽松
    # SameMajorVersion	主版本相同 且 已安装 ≥ 请求	语义化版本（SemVer）标准做法，你选的这个
    # SameMinorVersion	主+次版本都相同 且 已安装 ≥ 请求	非常保守，只允许补丁级差异
    # ExactVersion	必须完全相等	强绑定，几乎不用
# 注： 文件名必须严格是 <PackageName>ConfigVersion.cmake——大小写敏感，pansconfigversion.cmake 不会被识别。
#      必须和 pansConfig.cmake 装到同一目录
#       必须在 project() 之后调用
# COMPATIBILITY 是必填参数——漏写会直接报错，没有默认值
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/pansConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

# 生成器 ：把 install(TARGETS ... EXPORT pansTargets) 登记的目标清单，编译成一份可被 include() 执行的 CMake 脚本
# 将 pansTargets 中的各种目标都传递给了 pansTargets.cmake 
install(EXPORT pansTargets                  # 导出前面登记的目标集合
        FILE pansTargets.cmake              # 安装时生成 ansTargets.cmake  
        NAMESPACE pans::                    # 给导入后的目标名称添加前缀
        DESTINATION "${PANS_INSTALL_CMAKEDIR}" #导出文件的安装目录
)

# 把已经存在于构建目录里的文件，原样拷贝到安装目录
# 要安装的文件列表（必须是已存在的文件，否则 configure 阶段就报错）
# 这两个文件是自己的模板生成的
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/pansConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/pansConfigVersion.cmake"
    DESTINATION "${PANS_INSTALL_CMAKEDIR}"
)

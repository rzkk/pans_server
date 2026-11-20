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
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/pansConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

# 真正安装
install(EXPORT pansTargets              # 导出前面登记的目标集合
        FILE pansTargets.cmake          #生成的导入文件名
        NAMESPACE pans::               # 给导入后的目标名称添加前缀
        DESTINATION "${PANS_INSTALL_CMAKEDIR}" #导出文件的安装目录
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/pansConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/pansConfigVersion.cmake"
    DESTINATION "${PANS_INSTALL_CMAKEDIR}"
)

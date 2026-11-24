#ifndef PANS_INCLUDE_PANS_TYPE_NAME_H
#define PANS_INCLUDE_PANS_TYPE_NAME_H

#include <cstddef>
#include <source_location>
#include <string_view>

namespace pans::detail
{
// 探针函数
//提取T 的类型 
template <typename T>
constexpr std::string_view TypeNameProbe() noexcept
{
#if defined(_MSC_VER)
    return std::string_view{__FUNCSIG__};
#else   // 当成linux 平台
    return std::source_location::current().function_name();
    //return std::string_view{typeid(T).name()};
#endif
}

// 从MSVC的函数签名中提取类型名称
//三种编译器的格式
// GCC: consteval std::string_view pans::detail::TypeNameProbe() [with T = int;...]
// Clang: std::string_view pans::detail::TypeNameProbe() [T = int]
// MSCV: pans::detail::TypeNameProbe<int>(void) noexcept
[[nodiscard]] constexpr std::string_view ExtractMsvcTypeName(std::string_view signature) noexcept{
    //从这种格式中提取int  :    pans::detail::TypeNameProbe<int>(void) noexcept
    constexpr std::string_view marker = "TypeNameProbe<";
    constexpr std::string_view suffix= ">(void)";

    const std::size_t marker_pos = signature.rfind(marker);
    if (marker_pos == std::string_view::npos) {
        return signature;
    }
    const std::size_t begin = marker_pos + marker.size();
    const std::size_t end = signature.rfind(suffix);

    if (end == std::string_view::npos || end <= begin) {
        return signature;
    }
    return signature.substr(begin, end - begin);
}

template<class T>
[[nodiscard]] consteval std::string_view ExtractTypeName() noexcept{
    constexpr std::string_view signature = TypeNameProbe<T>();

    // GCC: consteval std::string_view pans::detail::TypeNameProbe() [with T = int;...]
    // Clang: std::string_view pans::detail::TypeNameProbe() [T = int]
    // MSCV: pans::detail::TypeNameProbe<int>(void) noexcept
#if defined(_MSC_VER) && !defined(__clang__)
    constexpr std::string_view type_name =  ExtractMsvcTypeName(signature);
    static_assert(type_name != signature, "Failed to extract type name");  // 在编译阶段就处理的
    return type_name;
#else 
    constexpr std::string_view  marker = "T = ";
    constexpr std::size_t marker_pos = signature.find(marker);
    if constexpr (marker_pos == std::string_view::npos) {
        return signature;
    }else{  // todo : find 与 rfind 
        constexpr std::size_t begin = marker_pos + marker.size();
        constexpr std::size_t semicolon = signature.find(';', begin); // 从begin开始找
        constexpr std::size_t bracket  = signature.find(']');
        //如果有分号，就以分号为准
        constexpr std::size_t end = (semicolon != std::string_view::npos) ? semicolon : bracket;
        if constexpr (end == std::string_view::npos || end <= begin) {
           return signature.substr(begin);
        }
        return signature.substr(begin, end - begin);
    }
    
   
#endif
}

// todo ? 这是什么语法 
template<class T>
inline constexpr std::string_view TYPE_NAME = ExtractTypeName<T>();

/**int  double 
 * inline constexpr std::string_view TYPE_NAME<int> = "int";
 * inline constexpr std::string_view TYPE_NAME<double> = "double";
 */



} // namespace pans::detail

namespace pans{
template<typename T>
[[nodiscard]] consteval std::string_view TypeName() noexcept{
    return detail::TYPE_NAME<T>;
}
}


#endif  // PANS_INCLUDE_PANS_TYPE_NAME_H

/**
 * 1  所有的查找都发生在编译阶段
 */
#ifndef PANS_INCLUDE_PANS_UTILS_STRING_UTILS_H
#define PANS_INCLUDE_PANS_UTILS_STRING_UTILS_H

#include <cstring>
#include <cstdarg>
#include <string_view>

#include <pans/export.h>

// 各个平台之间字符串 相互转换
namespace pans{

/**
 * @brief StringUtils类，来承载通用字符串操作。
 */
class PANS_API StringUtils final
{
public:
    // 使用delete禁用构造函数，它是一个纯静态类，所有的方法都是静态方法 ， 不会构造对象
    StringUtils() = delete;

    /**
     * @brief 宽字符转为UTF-8
     * 在Windows平台上，它会将UTF-16 宽字符wchar_t转成UTF8
     * 在Linux平台上，它会将UTF-32宽字符wchar_t转成UFT8
     * 
     * @param text 
     * @return std::string 转换失败时触发断言，返回空字符串
     */
    [[nodiscard]] static std::string WStringToString(std::wstring_view text) noexcept;

    /**
     * @brief UTF8字符串往宽字符转换
     * 在Windows平台上，wchar_t宽字符是UTF-16, 在Linux平台上，wchar_t宽字符是UTF-32
     * @param text 
     * @return std::wstring 转换失败时触发断言，返回空字符串
     */
    [[nodiscard]] static std::wstring StringToWString(std::string_view text) noexcept;
   
    // C++ 所有的空白字符，包括空格、制表符、换行符、回车符、换页符和垂直制表符
    inline static constexpr std::string_view ASCII_WHITESPACE{" \t\n\r\f\v"};
    //  用于字符串修剪的默认分隔符，即C++所有的空白字符  接口 string 
    [[nodiscard]] static std::string Trim(const std::string& text, std::string_view delimiters = ASCII_WHITESPACE);
    // 修剪字符串两端的空白字符 接口string_view 
    [[nodiscard]] static std::string_view Trim(std::string_view text, std::string_view delimiters = ASCII_WHITESPACE) noexcept;
    // 修剪字符串左端的空白字符 接口string
    [[nodiscard]] static std::string TrimLeft(const std::string& text, std::string_view delimiters = ASCII_WHITESPACE);
    // 修剪字符串左端的空白字符 接口string_view
    [[nodiscard]] static std::string_view TrimLeft(std::string_view text, std::string_view delimiters = ASCII_WHITESPACE) noexcept;
    
    // 修剪字符串右端的空白字符 接口string
    [[nodiscard]] static std::string TrimRight(const std::string& text, std::string_view delimiters = ASCII_WHITESPACE);

    // 修剪字符串右端的空白字符 接口string_view
    [[nodiscard]] static std::string_view TrimRight(std::string_view text, std::string_view delimiters = ASCII_WHITESPACE) noexcept;

};

}

#endif

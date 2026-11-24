#ifndef PANS_INCLUDE_PANS_CONFIG_CONVERT_H
#define PANS_INCLUDE_PANS_CONFIG_CONVERT_H

//处理常见类型 与   yaml::node 之间的转换

#include <charconv>
#include <concepts>
#include <cstddef>
#include <list>
#include <map>
#include <set>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <yaml-cpp/yaml.h>

#include <pans/type_name.h>
#include <pans/utils/string_utils.h>

namespace pans{
namespace detail{
//转错了 ，抛出异常 避免模板代码的迅速膨胀
[[noreturn]] PANS_API void ThrowConvertError(std::string_view type_name, std::string_view reason, std::string_view value = {});

//text 转 bool  : yes/no  true/false on/off 都是bool 
[[nodiscard]] PANS_API bool ParseBool(std::string_view text, bool& out) noexcept;

//从YAML节点中提取标量文本 ， 比如说 string 
[[nodiscard]] PANS_API const std::string& ScalarText(const YAML::Node& node, std::string_view type_name);

// todo : 这是什么语法
//  requires ： C++20 的模板参数约束，只有T 满足后面的条件，才符合这个模板 ： （是数值类型 and 不是bool ） 
//从 字符串txt 转换到数值类型
template <typename T>
requires std::is_arithmetic_v<T> && (!std::is_same_v<std::remove_cv_t<T>, bool>)
[[nodiscard]] T ParseArithmetic(std::string_view text, std::string_view type_name)
{
    std::string_view body = StringUtils::Trim(text); // 扣掉左右空白字符
    if(body.empty())
    {
        ThrowConvertError(type_name, "空标量无法解析为数值");
    }

    if(body.front() == '+')  // yaml 中允许前导的 '+'
    {
        body.remove_prefix(1);
    }

    T value{};
    std::from_chars_result result{};
    if constexpr (std::is_integral_v<T>)  // 判断是十进制还是十六进制
    {
        int base = 10;
        if(body.size() > 2 && body[0] == '0' && (body[1] == 'x' || body[1] == 'X'))
        {
            base = 16;
            body.remove_prefix(2);
        }
        result = std::from_chars(body.data(), body.data() + body.size(), value, base);
    }
    else
    {
        // 浮点数
        result = std::from_chars(body.data(), body.data() + body.size(), value);  // 使用这个实现 字符转数字
    }
    if(result.ec != std::errc{} || result.ptr != body.data() + body.size())  // 指针指向最后一个位置 ， 说明每个字符都参与转换了
    {
        ThrowConvertError(type_name, "无法解析为数值", text);
    }
    return value;
}

//数值转yaml node
template <typename T>
requires std::is_arithmetic_v<T> && (!std::is_same_v<std::remove_cv_t<T>, bool>)
[[nodiscard]] YAML::Node ArithmeticToNode(const T& value)
{
    char buffer[64]{};
    const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if(result.ec != std::errc{})
    {
        ThrowConvertError(TypeName<T>(), "数值类型无法格式化为字符串");
    }
    return YAML::Node(std::string(buffer, static_cast<std::size_t>(result.ptr - buffer)));  // 会触发匿名返回值优化 ， 不需要 std::move
}
// 模板参数不符合约束的时候 ， 报错信息超级 yongyu 上百行报错信息 
// c++ 20 引入 编译期约束 requires , 如果模板参数不满足条件 直接会在调用点处暴露出来
 // 还可以自己定义模板约束 ： 只在编译器检查   { ... } 是否合法 ，
 //  检查YAML::convert<T>::decode(node, out)  是否合法 ， 并且返回值是否能转到 std::convertible_to<bool
template <typename T>
concept YamlDecodable = requires(const YAML::Node& node, T& out)
{
    { YAML::convert<T>::decode(node, out) } -> std::convertible_to<bool>;
};

template <typename T>
concept YamlEncodable = requires(const T& value)
{
    { YAML::convert<T>::encode(value) } -> std::convertible_to<YAML::Node>;
};

} // namespace detail

// 配置值与yaml 之间的转换
template <typename T>
class ConfigConvert
{
public:
    //yaml node 转 T 
    [[nodiscard]] static T FromNode(const YAML::Node& node)
    {
        constexpr std::string_view type_name = TypeName<T>();
        if constexpr (std::is_same_v<T, bool>)
        {
            bool value = false;
            const std::string& text = detail::ScalarText(node, type_name);
            if(!detail::ParseBool(text, value))
            {
                detail::ThrowConvertError(type_name, "无法解析为布尔值", text);
            }
            return value;
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            return detail::ScalarText(node, type_name);
        }
        else if constexpr (std::is_arithmetic_v<T>)
        {
            return detail::ParseArithmetic<T>(detail::ScalarText(node, type_name), type_name);
        }
        else if constexpr (std::is_enum_v<T>)  // 枚举类型 ， 底层可以是 uint8也可以是uint16
        {
            using Underlying = std::underlying_type_t<T>;
            return static_cast<T>(detail::ParseArithmetic<Underlying>(detail::ScalarText(node, type_name), type_name));
        }
        else  // yaml 自己的转换能力 进行兜底
        {
            static_assert(detail::YamlDecodable<T>, "该类型需要特化 pans::ConfigConvert<T> 或 YAML::convert<T>");
            T value{};
            if(!node.IsDefined() || !YAML::convert<T>::decode(node, value))
            {
                detail::ThrowConvertError(type_name, "YAML::convert 解码失败");
            }
            return value;
        }
    }
    // T转 yaml node
    [[nodiscard]] static YAML::Node ToNode(const T& value)
    {
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
        {
            return detail::ArithmeticToNode(value);
        }
        else if constexpr (std::is_enum_v<T>)
        {
            return detail::ArithmeticToNode(static_cast<std::underlying_type_t<T>>(value));
        }
        else  // yaml 自己的转换能力 进行兜底
        {
            static_assert(detail::YamlEncodable<T>, "该类型需要特化 pans::ConfigConvert<T> 或提供 YAML::convert<T>::encode");
            return YAML::Node(value);
        }
    }
};

namespace detail{
// 接下来处理容器类型
// 能否在原地构造一个右值元素
template <typename C>
concept BackEmplaceable = requires(C& container, typename C::value_type&& value)
{
    container.emplace_back(std::move(value));
};
// 能否预留空间
template <typename C>
concept Reservable = requires(C& container, std::size_t count)
{
    container.reserve(count);
};

// 序列容器
template <typename Container>
class SequenceConvert
{
public:
    using value_type = typename Container::value_type;
    [[nodiscard]] static Container FromNode(const YAML::Node& node)
    {
        if(!node.IsSequence())
        {
            ThrowConvertError(TypeName<Container>(), "期望 YAML 序列节点");
        }

        Container container;
        if constexpr (Reservable<Container>)
        {
            container.reserve(node.size());
        }
        for(const YAML::Node& item : node)
        {
            if constexpr (BackEmplaceable<Container>)
            {
                container.emplace_back(ConfigConvert<value_type>::FromNode(item)); // 允许尾部插入
            }
            else
            {
                container.emplace(ConfigConvert<value_type>::FromNode(item));
            }
        }
        return container;      // NRVO :匿名返回值优化 ， 不需要 std::move  , 无需额外的拷贝或移动
        // Contianer result = FormNode(...); // 允许FormNode中的要返回的变量直接在result 处构造
        // 返回值类型 与要返回的变量类型一致 
    }

    [[nodiscard]] static YAML::Node ToNode(const Container& container)
    {
        YAML::Node node(YAML::NodeType::Sequence);
        for(const value_type& item : container)
        {
            node.push_back(ConfigConvert<value_type>::ToNode(item));
        }
        return node;
    }
};
// key : string 
template <typename Map>
class StringMapConvert
{
public:
    using mapped_type = typename Map::mapped_type;

    [[nodiscard]] static Map FromNode(const YAML::Node& node)
    {
        if(!node.IsMap())
        {
            ThrowConvertError(TypeName<Map>(), "期望 YAML 映射节点");
        }
        Map container;
        if constexpr (Reservable<Map>)
        {
            container.reserve(node.size());
        }
        for(const auto& entry : node)
        {
            if(!entry.first.IsScalar())
            {
                ThrowConvertError(TypeName<Map>(), "映射的键必须是标量");
            }
            container.emplace(entry.first.Scalar(), ConfigConvert<mapped_type>::FromNode(entry.second));
        }
        return container;
    }

    [[nodiscard]] static YAML::Node ToNode(const Map& container)
    {
        YAML::Node node(YAML::NodeType::Map);
        for(const auto& [key, value] : container)
        {
            node[key] = ConfigConvert<mapped_type>::ToNode(value);
        }
        return node;
    }
};
} // end of detail 

//下面是容器偏特化 
//下面的参数是为了对齐 容器的模板参数
// A ：Allocator 
// C : compare 
// H : hash
// E : equal 
template <typename T, typename A>
class ConfigConvert<std::vector<T, A>> : public detail::SequenceConvert<std::vector<T, A>>
{};

template <typename T, typename A>
class ConfigConvert<std::list<T, A>> : public detail::SequenceConvert<std::list<T, A>>
{};

// C是比较器
template <typename T, typename C, typename A>
class ConfigConvert<std::set<T, C, A>> : public detail::SequenceConvert<std::set<T, C, A>>
{};

template <typename T, typename H, typename E, typename A>
class ConfigConvert<std::unordered_set<T, H, E, A>> : public detail::SequenceConvert<std::unordered_set<T, H, E, A>>
{};

template <typename T, typename C, typename A>
class ConfigConvert<std::map<std::string, T, C, A>> : public detail::StringMapConvert<std::map<std::string, T, C, A>>
{};

template <typename T, typename H, typename E, typename A>
class ConfigConvert<std::unordered_map<std::string, T, H, E, A>> : public detail::StringMapConvert<std::unordered_map<std::string, T, H, E, A>>
{};

} // namespace pans

#endif

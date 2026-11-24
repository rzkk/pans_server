#ifndef PANS_INCLUDE_PANS_CONFIG_CONFIG_VAR_H
#define PANS_INCLUDE_PANS_CONFIG_CONFIG_VAR_H

#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>

#include <pans/config/convert.h>
#include <pans/types.h>

namespace pans {

using ListenerId = u64;  // 监听号

namespace detail {
// 统一报告配置系统内部的错误 // 公共头文件就不需要包含 logger  
// 报错更加统一 ， 缩短模板长度
PANS_API void ReportConfigError(std::string_view operation, std::string_view name, std::string_view type_name, std::string_view reason);

[[nodiscard]] PANS_API ListenerId NextListenerId() noexcept; // 线程安全的分配监听编号

[[nodiscard]] PANS_API std::string NormalizeConfigName(std::string name);// 标准化配置名称 。 统一成小写

[[nodiscard]] PANS_API bool IsValidConfigName(std::string_view name) noexcept;// 名称是否合法

} // namespace detail

// 配置项的基类
// 后面需要一个容器 ， 将不同类型的配置全都塞进去 ，提供增删改查接口 ， 使用抽象实现 sylar实现思路
class PANS_API ConfigVarBase
{
public:
    using ptr = std::shared_ptr<ConfigVarBase>;
    
    // 不用引用
    explicit ConfigVarBase(std::string name, std::string description = {});
    virtual ~ConfigVarBase(); // 虚基类 ， 父类指针指向子类时 能正确释放资源

    //保证配置的内存唯一
    ConfigVarBase(const ConfigVarBase&) = delete;
    ConfigVarBase& operator=(const ConfigVarBase&) = delete;
    ConfigVarBase(ConfigVarBase&&) = delete;
    ConfigVarBase& operator=(ConfigVarBase&&) = delete;

    [[nodiscard]] const std::string& getName() const noexcept { return m_name; }
    [[nodiscard]] const std::string& getDescription() const noexcept { return m_description; }

    [[nodiscard]] virtual YAML::Node toNode() const = 0;
    virtual bool fromNode(const YAML::Node& node) = 0;

    [[nodiscard]] std::string toString() const;  // TOnode 的文本封装
    bool fromString(std::string_view value);    // formNode 的文本封装

    [[nodiscard]] virtual std::string_view getTypeName() const noexcept = 0;//子类实现
    
    // 监听器相关接口
    virtual void removeListener(ListenerId id) = 0;
    virtual void clearListeners() = 0;
    [[nodiscard]] virtual std::size_t listenerCount() const = 0;

protected:
    std::string m_name;   // 成员名称   // 配置键值对 ： key - value // key 一定是string
    std::string m_description; // 成员描述
};

template <typename T, typename Convert = ConfigConvert<T>>
class ConfigVar final : public ConfigVarBase
{
public:
    using ptr = std::shared_ptr<ConfigVar>;
    using OnChange = std::function<void(const T& old_value, const T& new_value)>; // 检测配置变化的 回调函数 的类型

    ConfigVar(std::string name, T default_value, std::string description = {})
        : ConfigVarBase(std::move(name), std::move(description))
        , m_value(std::make_shared<const T>(std::move(default_value)))
    {}

    ~ConfigVar() override
    {
        clearListeners();
    }

    [[nodiscard]] T getValue() const { return *m_value.load(std::memory_order_acquire); }
    [[nodiscard]] std::shared_ptr<const T> getSnapshot() const noexcept
    {
        return m_value.load(std::memory_order_acquire);
    }

    bool setValue(T value)  // 值发生变化 通知监听器
    {
        auto new_value = std::make_shared<const T>(std::move(value));
        std::shared_ptr<const T> old_value;
        {
            std::lock_guard<std::mutex> lock(m_writeMutex);
            old_value = m_value.load(std::memory_order_relaxed);
            if constexpr (std::equality_comparable<T>)
            {
                if (*old_value == *new_value)
                {
                    return false;
                }
            }
            m_value.store(new_value, std::memory_order_release);
        }
        notify(*old_value, *new_value);
        return true;
    }

    [[nodiscard]] YAML::Node toNode() const override { return Convert::ToNode(*getSnapshot()); }

    bool fromNode(const YAML::Node& node) override
    {
        try
        {
            setValue(Convert::FromNode(node));
            return true;
        }
        catch (const std::exception& e)
        {
            detail::ReportConfigError("ConfigVar::fromNode", m_name, getTypeName(), e.what());
        }
        catch (...)
        {
            detail::ReportConfigError("ConfigVar::fromNode", m_name, getTypeName(), "未知异常");
        }
        return false;
    }

    [[nodiscard]] std::string_view getTypeName() const noexcept override { return TypeName<T>(); }

    ListenerId addListener(OnChange callback)
    {
        if (!callback)
        {
            return 0;
        }

        const ListenerId id = detail::NextListenerId();
        auto shared_callback = std::make_shared<const OnChange>(std::move(callback));
        {
            std::lock_guard<std::mutex> lock(m_listenersMutex);
            m_listeners.emplace_back(id, std::move(shared_callback));
        }
        return id;
    }

    void removeListener(ListenerId id) override
    {
        std::lock_guard<std::mutex> lock(m_listenersMutex);
        for (auto it = m_listeners.begin(); it != m_listeners.end(); ++it)
        {
            if (it->m_id == id)
            {
                m_listeners.erase(it);
                return;
            }
        }
    }

    void clearListeners() override
    {
        std::lock_guard<std::mutex> lock(m_listenersMutex);
        m_listeners.clear();
    }

    [[nodiscard]] std::size_t listenerCount() const override
    {
        std::lock_guard<std::mutex> lock(m_listenersMutex);
        return m_listeners.size();
    }

private:
    struct ListenerEntry
    {
        ListenerId m_id{0};
        std::shared_ptr<const OnChange> m_callback;
    };
//低频调用 ， 数据安全比性能更重要
    void notify(const T& old_value, const T& new_value) const
    {
        std::vector<ListenerEntry> snapshot;
        {
            std::lock_guard<std::mutex> lock(m_listenersMutex);
            snapshot = m_listeners;  //保证监听器在使用过程中不被别的进程删除 // 只能拷贝不能引用
        }
        for (const ListenerEntry& entry : snapshot)
        {
            try
            {
                (*entry.m_callback)(old_value, new_value);
            }
            catch (const std::exception& e)
            {
                detail::ReportConfigError("ConfigVar::listener", m_name, getTypeName(), e.what());
            }
            catch (...)
            {
                detail::ReportConfigError("ConfigVar::listener", m_name, getTypeName(), "未知异常");
            }
        }
    }

private:                                            // 类似于快照
    std::atomic<std::shared_ptr<const T>> m_value;  // 只有一个线程可以修改 shared_ptr ， 只能保证对指针的修改时是原子性的
    std::mutex m_writeMutex;  // 保证复杂操作的原子性
    mutable std::mutex m_listenersMutex; // 保护监听器列表的线程安全 // 保护m_listeners 的线程安全
    std::vector<ListenerEntry> m_listeners; 
};

} // namespace pans

#endif // PANS_INCLUDE_PANS_CONFIG_CONFIG_VAR_H

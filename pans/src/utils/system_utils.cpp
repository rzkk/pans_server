

#include<pans/utils/system_utils.h>
#include<sstream>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
     /*notes: WIN32_LEAN_AND_MEAN 
     * 排除掉哪些不常用且体积庞大的API声明
     * 还有排除掉shell相关，图形界面相关的一些冷门代码
     * 能大幅缩短编译时间，减少符号冲突
     * 这个宏需要在包含 <windows.h> 之前定义。
     * 主要作用是减少 Windows 头文件引入的一些不常用 API 声明，降低不必要的头文件依赖。
     */
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    // Windows 头文件可能定义：#define min(a, b) ... #define max(a, b) ... 这两个宏 ， 与C++ 标准库冲突
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif

    #include <windows.h>   // Windows 系统 API 和数据类型
    #include <dbghelp.h>  //调用栈符号、源码行号解析
#else
    #include <cxxabi.h>   //C++ 函数名反修饰
    #include <elfutils/libdwfl.h> //解析 ELF/DWARF 调试信息
    #include <execinfo.h>  //采集调用栈地址和获取符号描述 
    #include <unistd.h>    //提供 getpid() 等系统接口
#endif

namespace pans{
std::chrono::steady_clock::duration GetElapsedTime()noexcept{
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::steady_clock::now() - start;
}


std::uint64_t GetFiberId() noexcept{
    return 0;
}

//匿名namespace 只在这里可见
namespace  {

//debughelp 是windwows提供的一个调试帮助库，主要用于符号解析、堆栈跟踪 ,读取调试信息等功能。由于它不是线程安全的，所以在多线程环境下使用时需要加锁保护。
#if defined(_WIN32)  

// 获取调试帮助库的互斥锁
// 惰性初始化 
// 因为 DbgHelp 中包括 SymFromAddr()、SymGetLineFromAddr64() 等在内的符号处理接口不是线程安全的。
std::mutex& GetDbgHelpMutex(){
    static std::mutex mutex;
    return  mutex;
}
//RAII debughelp 库的初始化 和清理
class DbgHelpContext final{
public:
    DbgHelpContext():m_process(GetCurrentProcess()){
        // SYMOPT_UNDNAME ： 将符号名转换为未修饰的名称 （CPP 是允许重载的）
        // SYMOPT_DEFERRED_LOADS : 延迟加载符号 ， 不是一次性加载全部符号，用到的时候加载，提高加载速度
        // SYMOPT_LOAD_LINES  ： 加载行号信息，将地址映射到源码行
        SymSetOptions(SymGetOptions() | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES)    ;
        m_initialized =  SymInitialize(m_process, NULL, TRUE) == TRUE;  // 当前进程的符号初始化
    }
    ~DbgHelpContext(){
        if(m_initialized){
            SymCleanup(m_process);
        }
    }

    DbgHelpContext(const DbgHelpContext&) = delete;
    DbgHelpContext& operator=(const DbgHelpContext&) = delete;

    [[nodiscard]] HANDLE process() const noexcept{
        return m_process;
    }
    [[nodiscard]] bool initialized() const noexcept{
        return m_initialized;
    }

private:
    HANDLE m_process;
    bool m_initialized{false};
};

// 单例
DbgHelpContext & GetDbgHelpContext(){
    static DbgHelpContext context;
    return context;
}
// Windows 调用栈解析的核心函数。
//将一个栈帧地址（void*）转换成人类可读的函数名称、函数内偏移、源文件路径和代码行号，并写入 std::ostringstream。
void WriteWindowsFrame(std::ostringstream & stream , const DbgHelpContext& context , void * frame , bool is_return_address){
    // 1  把 void* 指针转换为能够容纳指针值的无符号整数类型。
    // 2  转换为 DbgHelp API 使用的 64 位地址类型
    auto address = static_cast<DWORD64>(
        reinterpret_cast<std::uintptr_t>(frame)   // 将 void* 指针转换为可以容纳指针值的无符号整数。
    );

    // 某些栈帧保存的是函数返回地址：即被调用函数执行结束后，调用者继续执行的位置。
    if(is_return_address && address > 0 ){  //回溯得到的某些栈帧地址是函数调用结束后应当继续执行的位置，也就是返回地址。
        --address;
    }

    //2. 尝试将地址转换为函数名称
    //  记录是否成功解析出符号。后面如果符号解析失败，就输出原始十六进制地址，保证至少有可用于调试的信息。
    bool symbol_found = false;
    if(context.initialized()) // 只有 DbgHelp 初始化成功才能执行符号查询。
    {
        /**SYMBOL_INFO_PACKAGE 是 Windows DbgHelp 提供的结构体。 
         * 其中包含： 1. SYMBOL_INFO si：保存符号元数据   2. 额外字符缓冲区：存储可能较长的符号名称
         * {} 是值初始化：将结构体成员初始化为零值。
         */
        SYMBOL_INFO_PACKAGE symbol_package{};
        symbol_package.si.SizeOfStruct = sizeof(SYMBOL_INFO);  //告诉 DbgHelp，传入的 SYMBOL_INFO 结构体有多大。
        symbol_package.si.MaxNameLen = MAX_SYM_NAME;        // 告诉 DbgHelp，为符号名称准备了多少字符的空间。 MAX_SYM_NAME 是 Windows 提供的最大符号名称长度常量。  

        /**displacement：当前查询地址相对于符号起始地址的字节偏移量。
         * 例如： 函数起始地址 = 0x1000  当前查询地址 = 0x1050
         * displacement = 0x50
         * DWORD64：64 位无符号整数。
         *  */ 
        DWORD64 displacement = 0;

        /** SymFromAddr()：根据指定的指令地址查询对应的符号信息。  // 查询函数名
         * 参数 1：context.process() ： 之前传给 SymInitialize() 的进程句柄。
         * 参数 2：address ： 要查询的指令地址。
         * 参数 3：&displacement  ： 输出参数，接收地址相对于符号起始位置的偏移。
         * 参数 4：&symbol_package.si ：输入/输出参数。调用前提供结构体大小、名称缓冲区大小；调用成功后，获得函数名及其他符号信息。
         * 返回 TRUE 表示查询成功，FALSE 表示失败。
         */
        if(SymFromAddr(context.process(), address, &displacement, &symbol_package.si) == TRUE)
        {
            /** Name 是 SYMBOL_INFO 中保存符号名称的字符数组。
             * 若初始化符号引擎时启用了 SYMOPT_UNDNAME， C++ 修饰名通常可以转换成更易读的名称。
             * 例如 ： pans::Server::HandleRequest
             */
            stream << symbol_package.si.Name;
            // 如果当前地址不在函数的起始位置，则在函数名后面追加偏移量。
            if(displacement != 0)
            {
                stream << "+0x"    //"+0x" 是字符串前缀，std::hex 本身不会自动输出 "0x"。
                << std::hex        // std::hex：后续整数按十六进制输出
                << displacement 
                << std::dec;        //std::dec：将后续整数输出恢复成十进制。
            }
            symbol_found = true;   // 标记已经成功解析出函数符号。 后面不需要再输出原始地址作为替代。
        }
    }
    /**3. 函数符号解析失败时，输出原始地址
     * 可能失败的原因：   1. DbgHelp 初始化失败
     *                  2. 对应模块的符号不可用
     *                  3. 地址没有匹配到符号
     *                  4. 符号查询 API 本身失败
     * 即使没有函数名，也保留指令地址用于调试。
     *  */ 

    if(!symbol_found)
    {
        stream << "0x" << std::hex << address << std::dec;
    }
    /** 4. 继续尝试解析源文件和行号
     *  源码行号查询和函数名查询是两个不同操作。
     *  即使 SymFromAddr() 失败， 也可以继续尝试 SymGetLineFromAddr64()。
     *  */
    if(context.initialized())
    {
        /**IMAGEHLP_LINE64 用于保存源码位置相关信息。 
         * 包括： - LineNumber：源码行号     - FileName：源文件路径    - Address：对应源码行的起始指令地址
         */
        IMAGEHLP_LINE64 line{};

        line.SizeOfStruct = sizeof(line); // 使用 DbgHelp API 前，必须设置结构体大小。
        DWORD displacement = 0;  // 这里的 DWORD displacement 是源码行偏移。

        /**SymGetLineFromAddr64()： 根据指令地址查询对应的源文件和代码行号。
         *                          参数 1：进程句柄
         *                          参数 2：指令地址
         *                          参数 3：输出当前地址相对于源码行起始地址的偏移
         *                          参数 4：接收源码位置的 IMAGEHLP_LINE64 结构体
         * 这里不需要使用返回的行内 displacement，但必须提供用于接收它的变量地址。
         */
        if(SymGetLineFromAddr64(context.process(), address, &displacement, &line) == TRUE && line.FileName != nullptr)
        {
            stream << " at " << line.FileName << ':' << line.LineNumber;
        }
    }
}
#else 

// 自定义的删除器
struct FreeDeleter final
{
    void operator()(void* ptr) const noexcept
    {
        std::free(ptr);
    }
};

// Dwfl 是 elfutils 库管理的调试信息上下文 , 它不能直接使用普通的 delete 释放，而需要：dwfl_end
struct DwflDeleter final
{
    void operator()(Dwfl* dwfl) const noexcept
    {
        dwfl_end(dwfl);
    }
};

/**
 * @brief libdwfl 的回调函数配置表
 * ELF 是文件格式 :可执行与可链接文件格式
 * Dwfl_Callbacks 是 elfutils 定义的结构体。
 * 这里告诉 libdwfl：
 * 1. 如何定位进程加载的 ELF 文件；
 * 2. 如何查找 DWARF 调试信息；
 * 3. 是否需要提供额外的 ELF 节地址解析；
 * 4. 是否指定自定义调试信息搜索路径。
 *
 * const：回调配置初始化后不允许修改。
 */
const Dwfl_Callbacks DWFL_CALLBACKS{
    dwfl_linux_proc_find_elf,           // 用于从正在运行的进程中查找 并打开ELF 文件 ，读取procPIDmaps 文件，用于定位模块对应的磁盘文件
    dwfl_standard_find_debuginfo,      
    nullptr,
    nullptr
};

/**Parser 中文:词法解析器
 * 把程序运行时的指令地址转换为源代码中的文件路径和行号。（源码位置解析器（Source Location Resolver） ) 
 * 实现依赖：
 * - ELF：    提供可执行文件及动态库的结构信息；
 * - DWARF：  提供地址与源码文件、行号之间的映射；
 * - libdwfl：负责进程模块管理与调试信息查找。
 * 
 *  */
class SourceLocationParser final
{
public:
    /**1. 构造函数：创建并初始化 libdwfl 上下文
     *      dwfl_begin(&DWFL_CALLBACKS)：创建一个新的 Dwfl 调试会话。返回一个指向新创建的 Dwfl 结构的指针，如果创建失败则返回 nullptr。
     *                  参数： &DWFL_CALLBACKS 是 回调配置表的地址。
     *      m_dwfl 是 unique_ptr<Dwfl, DwflDeleter>，会接管返回的 Dwfl*。
     */
    SourceLocationParser() : m_dwfl(dwfl_begin(&DWFL_CALLBACKS)){
        /** 这里用 || 连接多个初始化检查。
         * 第一个条件：!m_dwfl 判断 dwfl_begin() 是否创建失败。
         * 第二个条件：dwfl_linux_proc_report( m_dwfl.get(), getpid())
                *                      m_dwfl.get() :libdwfl 上下文。
                *                  getpid()：获取当前进程的 PID（进程 ID）。
                *                   dwfl_linux_proc_report()：读取 /proc/self/maps ,将当前进程加载的所有 ELF 模块（可执行文件 + 动态库）登记到 Dwfl 上下文
         *                          返回值： 
         *                                  成功返回 0，  成功报告进程模块。
         *                                  失败返回非 0。进程模块登记失败
         *                                               可能是无法读取 /proc/<pid>/maps、进程信息或 ELF 模块信息，或者其他 libdwfl 初始化问题。
         * 第三个条件：dwfl_report_end(m_dwfl.get(), nullptr, nullptr) ： 结束模块登记阶段，让 libdwfl 完成内部整理
         *                      返回值：成功返回 0，失败返回非 0。
         * 三个条件只要有一个为 true，就说明初始化不完整或失败。
         */
        if(!m_dwfl || dwfl_linux_proc_report(m_dwfl.get(), getpid()) || dwfl_report_end(m_dwfl.get(), nullptr, nullptr))
        {
            /** reset()：如果 m_dwfl 持有 Dwfl*，就调用 DwflDeleter 释放它。
             * 释放后 m_dwfl 变为空。后续 parse() 将返回空字符串，不会继续使用无效上下文。 
             * 这里是给日志用的 ， 即便是失败了， 也不能抛异常
             * m_dwfl ： 是unique_ptr 
            */
            m_dwfl.reset();
        }
    }
/**2. parse()：解析一条栈帧的源码位置 ，将单个栈帧地址转换为源码文件和行号
 * frame ：当前栈帧地址，通常来自 backtrace()。
 * is_return_address ：是否将地址视为返回地址。true 时通常减 1，以定位调用指令。
 *  @return std::string
 *      成功：文件路径:行号
 *      失败：空字符串
 */
    [[nodiscard]] std::string parse(void* frame, bool is_return_address) const{
        if(!m_dwfl)  // 2.1 检查 Dwfl 上下文是否有效 ，如果初始化失败，就直接返回空字符串。
        {
            return {};
        }
        //2.2 将指针转换为 DWARF 地址类型
        // reinterpret_cast<std::uintptr_t>(frame)：将 void* 转换为能够容纳指针值的无符号整数。
        // static_cast<Dwarf_Addr>(...)：再转换为 libdwfl 接口要求的地址类型。
        // Dwarf_Addr 是 DWARF 地址整数类型。
        // 注意：这里转换的是地址值， 并没有解引用 frame 指向的内存。
        auto address = static_cast<Dwarf_Addr>(reinterpret_cast<std::uintptr_t>(frame));
        /**
         * 2.3 必要时调整返回地址
         *  backtrace() 返回的后续栈帧通常对应返回地址。
         *  返回地址一般指向调用指令结束之后的位置。 减 1 有助于将位置调整到调用指令内部， 从而得到更符合预期的源码行号。
         *  address > 0 防止无符号整数下溢。
         */
        if(is_return_address && address > 0)
        {
            --address;
        }

        /**2.4 根据地址查询源码行记录
         * dwfl_getsrc()：在已报告的模块及其 DWARF 信息中，查找 address 对应的源码行记录。
         * 参数一：m_dwfl.get() 返回 Dwfl*。
         * 参数二：address 为目标指令地址。
         * 返回： 成功 -> Dwfl_Line*
         *       失败 -> nullptr
         * Dwfl_Line 是 libdwfl 管理的行信息对象。
         * 不应直接 delete 或 free 它。
         */
        Dwfl_Line* line = dwfl_getsrc(m_dwfl.get(), address);
        /**  没找到对应源码行信息。
         * 可能原因：
         *  - 没有 DWARF 调试信息
         *  - 地址未对应到已知模块
         *  - 相关地址没有源码行映射
         */
        if(line == nullptr)
        {
            return {};
        }
        // 2.5 提取文件名与行号
        int line_number = 0;
        /**  dwfl_lineinfo()：从 Dwfl_Line 记录中提取源码位置信息。
         * 参数一：line 已查询到的源码行记录。 这一行的信息全在里面
         * 参数二：nullptr 不需要获取源码行对应的地址。
         * 参数三：&line_number获取源码行号的输出参数。
         * 参数四：nullptr 不需要获取列号。
         * 参数五：nullptr 不需要获取源码文件修改时间。
         * 参数六：nullptr 不需要获取源码文件长度。
         * 返回值：
         * 文件名的 const char*；
         * 无法获取时可能返回 nullptr。
        */
        const char* file_name = dwfl_lineinfo(line, nullptr, &line_number, nullptr, nullptr, nullptr);
        if(file_name == nullptr || line_number <= 0)
        {
            return {};
        }
        return std::string(file_name) + ':' + std::to_string(line_number);
    }

private:
    std::unique_ptr<Dwfl, DwflDeleter> m_dwfl; 
    //这个智能指针独占一个 Dwfl 上下文，并在生命周期结束时自动调用 dwfl_end()。
    // 每个进程都会构造一个 Dwfl ， 不需要加锁
};


/**从 Linux 调用栈描述中提取 C++ 函数名称
 *                                                           模块名(符号名 + 偏移) [地址]
 * symbol ： backtrace_symbols() 返回的单条栈帧描述。 典型格式：./server(_ZN4pans7ProcessEi+0x25) [0x401250]
 * return ：  1. 成功反修饰：返回可读的 C++ 函数名。
 *           2. 无法提取符号：返回原始描述。
 *           3. 反修饰失败：返回提取出来的原始符号名。 
 * 该函数只处理单条调用栈描述，不负责采集栈帧。  
 */
std::string DemangleBacktraceSymbol(const char* symbol)
{
 //1. 将 C 风格字符串包装成 std::string_view
    //symbol 是 const char*，可能为空指针。 
    const std::string_view text(symbol == nullptr ? "" : symbol);

//2. 查找函数符号的起始位置
    /**Linux backtrace_symbols() 常见输出：
     *              ./server(_ZN4pans7ProcessEi+0x25) [0x401250]
     *                      ^
     *          name_begin 指向这个 '('
     */
    const auto name_begin = text.find('(');
    // 如果没有左括号，说明无法按照预期格式
    // 提取函数符号，直接保留原始字符串。
    if(name_begin == std::string_view::npos)
    {
        return std::string(text);
    }

// 3. 确定函数符号的开始和结束位置
    //从 mangled_begin 开始查找 '+'。
    // 典型格式： (函数符号名+偏移量)  ，  '+' 通常表示函数名已经结束，后面是当前地址相对于符号起始位置的偏移。
    const auto mangled_begin = name_begin + 1;
    auto name_end = text.find('+', mangled_begin);

    //部分符号描述可能没有 '+偏移量'。如果找不到 '+'，则尝试使用右括号 ')' ， 作为函数符号的结束位置。
    if(name_end == std::string_view::npos)
    {
        name_end = text.find(')', mangled_begin);
    }

    // 判断符号范围是否合法。
    // 条件一： name_end == npos 没有找到结束位置。
    // 条件二： name_end == mangled_begin 起始位置与结束位置相同，符号名为空。
    // || 是逻辑或，任意条件成立就返回 true。
    if(name_end == std::string_view::npos || name_end == mangled_begin)
    {
        return std::string(text);
    }
// 4. 提取经过 C++ 名称修饰的函数符号
    const std::string mangled_name(text.substr(mangled_begin, name_end - mangled_begin));
    int status = 0;
// 5. 使用 C++ ABI 接口进行名称反修饰
    /** status：用于接收 __cxa_demangle() 的执行状态。    
     *   0  ：成功
     *  -1 ：内存分配失败
     *  -2 ：输入不是合法的 ABI 修饰名
     *  -3 ：参数无效*/
    /** abi::__cxa_demangle()：
     *  将修饰后的 C++ 符号转换为可读名称。
     *  参数一： mangled_name.c_str() 获取以 '\0' 结尾的 C 风格字符串。
     *  参数二  nullptr： 不提供现成的输出缓冲区， 由函数自行申请内存。
     *  参数三   nullptr：不需要接收输出缓冲区的长度。
     *  参数四   &status：传入状态变量的地址，用来接收执行结果。
     *  返回值： char*，指向新分配的字符串缓冲区。
     *  这块内存必须使用 free() 释放。所以使用 unique_ptr + 自定义删除器管理。*/
    std::unique_ptr<char, FreeDeleter> demangled_name(abi::__cxa_demangle(mangled_name.c_str(), nullptr, nullptr, &status));
    if(status == 0 && demangled_name){
        return demangled_name.get();
    }
    // C 函数名不需要 demangle
    return mangled_name;
}


#endif
} // namespace 



/** notes: 获取当前线程的函数调用栈，并格式化为字符串
 *  
 * @param size   最多采集的栈帧数量（包括后续需要跳过的帧）
 * @param skip   跳过最前面的栈帧数量
 * @param prefix 每行调用栈信息的前缀，例如 "    "
 * @note Windows：CaptureStackBackTrace + DbgHelp
 * @note Linux：  backtrace + backtrace_symbols + libdwfl
 */
std::string GetBacktrace(int size, int skip, const std::string& prefix)
{
    if(size <= 0)
    {
        return {};
    }

#if defined(_WIN32)
    // USHORT 是 Windows 的无符号短整数类型
    // 控制取的栈帧层数要小于 ushort 的最大值 (65535)
    const int capture_size = std::min(size, 
                                    static_cast<int>(           //static_cast<int>() 将该值显式转换成 int
                                        std::numeric_limits<USHORT>::max() 
                                    ));
    // 存放栈帧地址 
    // void*：通用指针，可保存栈帧地址。
    // std::size_t ： 无符号 ， 64位 , 与uint64_t 一样
    std::vector<void*> frames(static_cast<std::size_t>(capture_size));

/**notes:
 *   CaptureStackBackTrace：Windows 提供的栈回溯函数。
        参数 1： 0 从最前面的栈帧开始采集，不在 API 层跳过。
        参数 2： frames.size() 最多采集多少层。
        参数 3： frames.data() 提供数组首地址，让 API 写入栈帧地址。
        参数 4： nullptr 不要求计算调用栈哈希值。
        返回值： 实际采集到的栈帧数量。
        DWORD : 32 位无符号整型（32-bit unsigned integer），占 4 个字节
        DWORD64 : 64位无符号整型 std::uint64_t*/
    const int frame_count = static_cast<int>(
        CaptureStackBackTrace(0, static_cast<DWORD>(frames.size()), frames.data(), nullptr)
    );

    if(frame_count <= 0)
    {
        return {};
    }

//notes:  3. Windows：准备符号解析
    std::ostringstream stream;
    // 将 skip 限制在 [0, frame_count] 之间。
    // skip < 0          -> 0
    // skip > frame_count -> frame_count
    // 其他情况           -> skip
    // 避免非法数组索引。
    /**std::clamp : C++17 , <algorithm> ,  将一个值限制在指定的上下界区间 [lo, hi] 内
     * 如果目标值小于下界 lo，则返回 lo；
     * 如果目标值大于上界 hi，则返回 hi；
     * 如果在区间内，则返回目标值本身。
     *  */ 
    const int first_frame = std::clamp(skip, 0, frame_count);

    // DbgHelp 符号解析接口不是线程安全的。因此使用全局共享互斥锁保护符号解析过程。
    std::lock_guard<std::mutex> lock(GetDbgHelpMutex());
    
    // 获取 DbgHelp 上下文
    // DbgHelpContext 单例 
    const DbgHelpContext& context = GetDbgHelpContext();

    for(int i = first_frame; i < frame_count; ++i)
    {
        stream << prefix << "#" << i - first_frame << " ";
        /**根据栈帧地址解析： 1. 函数名称  2. 函数内部偏移  3. 源文件路径 4. 行号
         * rames[i]：当前栈帧地址 
         * i > 0： 表示当前是否按返回地址进行处理。 对后续栈帧，辅助函数可能将地址减 1，以便定位到调用指令，而不是下一条指令。
         */
        WriteWindowsFrame(stream, context, frames[i], i > 0);
        stream << '\n'; // 每个栈帧占一行。'\n' 是换行字符，不会强制刷新输出流。
    }

    return stream.str();
#else
// 2. Linux：获取栈帧地址
    std::vector<void*> frames(static_cast<std::size_t>(size));
    /**backtrace()：获取当前线程的调用栈。是 glibc 提供的函数。 :: 表示明确使用全局命名空间中的 backtrace。
     * 参数 1：frames.data() 提供数组首地址，让 API 写入栈帧地址。
     * 参数 2：size 最多采集多少层。
     * 返回值：实际采集到的栈帧数量。
     */
    const int frame_count = ::backtrace(frames.data(), size);

    if(frame_count <= 0){  // 采集失败或没有得到栈帧。
        return {};
    }
//3. Linux：将栈帧地址转换为符号描述
    /**backtrace_symbols()：把栈帧地址数组转换为符号描述字符串数组。是 glibc 提供的函数。
     *              参数 1：frames.data() 提供栈帧地址数组。
     *              参数 2：frame_count 栈帧数量。
     * 返回值：  char** 指向符号描述字符串的指针数组。
     *          可以将它理解为：  symbols[0] -> 第一条栈帧描述
     *                           symbols[1] -> 第二条栈帧描述
     * backtrace_symbols() 分配的内存必须通过 free() 释放。因此使用带有自定义删除器的 unique_ptr。
     * unique_ptr<char*, FreeDeleter> 管理的是 char**。 离开作用域时会调用 FreeDeleter 中的 free()。
     */
    std::unique_ptr<char*, FreeDeleter> symbols(::backtrace_symbols(frames.data(), frame_count));
    if(!symbols)   //如果 backtrace_symbols() 分配失败，就返回空字符串。
    {
        return {}; 
    }

// 4. Linux：准备源码位置解析器
    std::ostringstream stream;
    /**SourceLocationParser ： 构造时初始化 libdwfl 上下文， 登记当前进程加载的 ELF 模块。
     *                      后续利用 DWARF 调试信息，将栈帧地址转换为源码文件名和行号。
     */
    const SourceLocationParser source_location_parser;
    const int first_frame = std::clamp(skip, 0, frame_count);// 限制 skip 范围，防止数组越界。

// 5. Linux：遍历栈帧
    for(int i = first_frame; i < frame_count; ++i)
    {
        /** symbols.get() 返回被 unique_ptr 管理的 char**。  通过解引用 symbols.get()，可以访问到符号描述字符串数组。
         * symbols.get()[i]： 获取第 i 个栈帧的 C 风格字符串。
         * 
         * DemangleBacktraceSymbol()：尝试将 C++ 修饰后的函数名还原为可读名称。
         * 例如： _ZN4pans7ProcessEi  转换为： pans::Process(int)*/
        stream << prefix << "#" << i - first_frame << " " << DemangleBacktraceSymbol(symbols.get()[i]);

        /**解析当前地址对应的源码位置。 frames[i]：第 i 个原始栈帧地址。 
         * i > 0：对于后续返回地址，解析器可能将地址减 1，避免定位到调用指令之后。 
         * 返回值可能是："/project/src/server.cpp:128"
         * 如果解析失败，则返回空字符串。
         */
        const std::string source_location = source_location_parser.parse(frames[i], i > 0);
        // 只有成功获取文件和行号时才追加。
        if(!source_location.empty())
        {
            stream << " at " << source_location;
        }
        stream << '\n';  // 输出当前栈帧的结束换行符。
    }
    return stream.str();
#endif
}

} // namespace pans



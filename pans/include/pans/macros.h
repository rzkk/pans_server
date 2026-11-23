#ifndef PANS_INCLUDE_PANS_MACROS_H
#define PANS_INCLUDE_PANS_MACROS_H

#include<cassert> 
// #include<iostream>
#include<pans/logger/log_logger.h>
#include<pans/utils/system_utils.h>
#include<pans/logger/log_line.h>
// # 注意release 版本是不带符号信息的
// # 所以调用堆栈信息无法得到 行号等信息
// # tests/test.cpp:7 ASSERT x > 0 failed .[  x must be positive ].
// #  Stacktrace: 
// # #0 func2(int)
// # #1 main
// # #2 __libc_start_main
// # #3 _start

// 对外提供的接口 ： x 为false 时进入断言
// unlikely : c++20 , 告诉编译器这个条件极大概率不成立

//带说明文字的assert  , 带有info参数 
// std::cerr<<__FILE__<<":"<<__LINE__<<" ASSERT "<< #x <<" failed .[  "<<  w <<" ].\n Stacktrace: \n"<< pans::GetBacktrace();

#define PANS_ASSERT(x,w)\
    if(!(x)) [[unlikely]]{ \
        PANS_LOG_FATAL(PANS_LOG_ROOT()) << "Assert " << #x << " failed. [" << w << "].\nStacktrace:\n" << pans::GetBacktrace();\
        assert(x); \
    } 

//函数中的assert ,带有返回值的
#define ASSERT_RETVAL(x , val , info)\
    do{ \
        if(x)[[likely]] break; \
        PANS_ASSERT(x ,info); \
        return val; \
    }while(0)
//函数中的assert ,无返回值的
#define ASSERT_RETNONE(x  , info)\
    do{ \
        if(x)[[likely]] break; \
        PANS_ASSERT(x ,info); \
        return ; \
    }while(0)

//函数中的assert 后不return  // 仅仅只是想出现这种情况提示一下下  // todo 但是现在还是会终止
#define ASSERT_NOEFFECT(x , info)\
    do{ \
        if(x)[[likely]] break; \
        PANS_ASSERT(x ,info); \
    }while(0)


// 循环中的 assert 
//x true : 正常
#define ASSERT_CONTINUE(x , info) \
    if(!(x))  [[unlikely]] {\
        PANS_ASSERT(x , info); \
        continue; \
    } else {}

#define ASSERT_BREAK(x , info) \
    if(!(x))  [[unlikely]] {\
        PANS_ASSERT(x , info); \
        break; \
    } else {}  






#endif
#ifndef PANS_INCLUDE_PANS_MACROS_H
#define PANS_INCLUDE_PANS_MACROS_H

#include<cassert> 
#include<iostream>

#include<pans/utils/system_utils.h>

// 对外提供的接口 ： x 为false 时进入断言
// unlikely : c++20 , 告诉编译器这个条件极大概率不成立

//带说明文字的assert  , 带有info参数
#define PANS_ASSERT(x,w)\
    if(!(x)) [[unlikely]]{ \
        std::cerr<<__FILE__<<":"<<__LINE__<<" ASSERT "<< #x <<" failed .[  "<<  w <<" ].\n Stacktrace: \n"<< pans::GetBacktrace();\
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
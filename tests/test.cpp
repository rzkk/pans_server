#include<pans/logger/log_level.h>
#include<iostream>
#include<pans/macros.h>

void func2(int x ){
    std::cout<<x<<std::endl;
    ASSERT_RETNONE(x > 0, "x must be positive");
}

void func1(int x ){
    std::cout<<x<<std::endl;
    func2(x);
}
int main(){
    
    std::cout<< sizeof( std::size_t)<< std::endl;
    std::cout<< sizeof( std::uint64_t)<< std::endl;
    func1(0);
    std::cout<<__FILE__<<std::endl;
}
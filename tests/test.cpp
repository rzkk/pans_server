#include<pans/logger/log_level.h>
#include<iostream>
int main(){
    pans::LogLevel::Level level = pans::LogLevel::Level::LOG_LV_DEBUG;

    std::cout << pans::LogLevel::ToString(level) << std::endl;
}
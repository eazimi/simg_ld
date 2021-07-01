#include <iostream>
#include <csignal>
#include "app_loader.h"

using namespace std;

int main(int argc, char **argv, char **env)
{
    std::cout << "Hello World!" << std::endl;
    unique_ptr<AppLoader> appLoader(new AppLoader());     
    pair<void *, void *> range;
    appLoader->getReservedMemRange(range);
    std::cout << "getReservedArea(): start = " << std::hex << range.first << " , end = " << range.second << std::endl;
    std::cout << "memory layout BEFORE unmmap ..." << std::endl;
    appLoader->printMMappedRanges();
    // do munmap    
    std::cout << "memory layout AFTER unmmap ..." << std::endl;
    appLoader->memUnmapRanges();    
    appLoader->printMMappedRanges();
    raise(SIGINT);
    return 0;
}
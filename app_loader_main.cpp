#include <iostream>
#include <csignal>
#include "app_loader.h"
#include "global.hpp"

using namespace std;

int main(int argc, char **argv, char **env)
{
    unique_ptr<AppLoader> appLoader(new AppLoader());     
    pair<void *, void *> range;
    std::cout << "[CHILD], memory layout BEFORE unmmap ..." << std::endl;
    print_mmapped_ranges();
    // do munmap
    std::cout << "[CHILD], memory layout AFTER unmmap ..." << std::endl;
    appLoader->memUnmapRanges();    
    print_mmapped_ranges();
    return 0;
}
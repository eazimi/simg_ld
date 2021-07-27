#include <iostream>
#include <csignal>
#include "app_loader.h"
#include "global.hpp"

using namespace std;

int main(int argc, char **argv, char **env)
{
    cout << "in app_loader main()" << endl;
    unique_ptr<AppLoader> appLoader(new AppLoader());    
    // std::cout << "[CHILD], memory layout BEFORE unmmap ..." << std::endl;
    // print_mmapped_ranges();
    // do munmap
    // std::cout << "[CHILD], memory layout AFTER unmmap ..." << std::endl;
    appLoader->release_parent_memory_region();    
    // print_mmapped_ranges();
    return 0;
}
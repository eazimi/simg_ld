#include "app_loader.h"
#include "global.hpp"
#include <csignal>
#include <iostream>

using namespace std;

int main(int argc, char** argv, char** env)
{
  // cout << "in app_loader main()" << endl;
  // cout << "argc: " << argc << " # argv[0]: " << argv[0] << " # argv[argc-1]: " << argv[argc-1] << endl;
  unique_ptr<AppLoader> appLoader(new AppLoader());
  // std::cout << "[CHILD], memory layout BEFORE unmmap ..." << std::endl;
  // print_mmapped_ranges();
  // do munmap
  // std::cout << "[CHILD], memory layout AFTER unmmap ..." << std::endl;
  appLoader->release_parent_memory_region();
  // print_mmapped_ranges();
  return 0;
}
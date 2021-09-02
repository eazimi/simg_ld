#include "app_loader.h"
#include "global.hpp"
#include <csignal>
#include <iostream>

using namespace std;

int main(int argc, char** argv, char** env)
{
  // cout << "in app_loader main()" << endl;
  // cout << "argc: " << argc << " # argv[0]: " << argv[0] << " # argv[argc-1]: " << argv[argc-1] << endl;
  unique_ptr<AppLoader> appLoader(new AppLoader(argv[argc - 1]));
  return 0;
}
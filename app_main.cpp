#include "app.h"
#include "global.hpp"
#include <csignal>
#include <iostream>

using namespace std;

int main(int argc, char** argv, char** env)
{
  cout << "main in app_main.cpp" << endl;
  // while(true);
  // cout << "argc: " << argc << " # argv[0]: " << argv[0] << " # argv[argc-1]: " << argv[argc-1] << endl;
  unique_ptr<App> appLoader(new App(argv[argc - 1]));
  return 0;
}
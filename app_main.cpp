#include "app.h"
#include "global.hpp"
#include <csignal>
#include <iostream>

using namespace std;

int main(int argc, char** argv, char** env)
{
  // cout << "app_main.cpp->main()" << endl;
  unique_ptr<App> appLoader(new App(argv[argc - 1]));
  return 0;
}
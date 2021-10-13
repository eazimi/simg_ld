#include "cmdline_params.h"
#include <iostream>

// returns the parent's parameters start index in the command line parameters
int cmdLineParams::process_argv(char** argv)
{
  // auto p = argv;
  // cout << "in process_argv(): " << endl;
  // auto parCount = 0;
  // while(*p != nullptr)
  // {
  //   cout << *p << endl;
  //   ++parCount;
  //   ++p;
  // }
  // cout << "parameter count: " << parCount << endl;

  vector<string> argv1, argv2;
  auto* args          = &argv1;
  bool separatorFound = false;
  auto i{0};
  auto index{0};
  argv++;
  i++;
  while (*argv != nullptr) {
    if (strcmp(*argv, (char*)"--") == 0) {
      separatorFound = (!separatorFound) ? true : false;
      if (!separatorFound)
        return -1;
      index = i;
      args  = &argv2;
    } else
      args->push_back(*argv);
    argv++;
    i++;
  }

  // todo: check for more condition, like app's parameter count
  if (!separatorFound || argv1.empty() || argv2.empty())
    return -1;
  apps_.push_back(argv1); // child
  apps_.push_back(argv2); // parent
  // cout << "-- in: " << index + 1 << endl;
  return ++index;
}
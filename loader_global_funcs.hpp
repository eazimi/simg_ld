#ifndef LOADER_GLOBAL_FUNCS_HPP
#define LOADER_GLOBAL_FUNCS_HPP

#include "loader_headers.hpp"

// returns the parent's parameters start index in the command line parameters
static int process_argv(const char** argv, pair<int, int>& param_count)
{
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
  param_count.first  = argv1.size(); // child process
  param_count.second = argv2.size(); // parent process
  return ++index;
}

#endif
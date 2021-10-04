#ifndef CMDLINE_PARAMS_HPP
#define CMDLINE_PARAMS_HPP

#include <utility>
#include <vector>
#include <string>
#include <cstring>

using namespace std;

class cmdline_params {
private:
public:
  explicit cmdline_params() = default;
  int process_argv(char** argv, pair<int, int>& param_count);
};

#endif
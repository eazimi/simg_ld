#ifndef CMDLINE_PARAMS_HPP
#define CMDLINE_PARAMS_HPP

#include <utility>
#include <vector>
#include <string>
#include <cstring>

using namespace std;

class cmdline_params {
private:
  vector<vector<string>> apps_;

public:
  explicit cmdline_params() = default;
  int process_argv(char** argv, pair<int, int>& param_count);
  inline int getAppCount() const { return apps_.size(); } 
  inline vector<string> getAppParams(int index) const { return apps_[index]; } 
  inline int getAppParamsCount(int index) const { return apps_[index].size(); } 
};

#endif
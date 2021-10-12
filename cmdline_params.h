#ifndef CMDLINE_PARAMS_HPP
#define CMDLINE_PARAMS_HPP

#include <utility>
#include <vector>
#include <string>
#include <cstring>

using namespace std;

class cmdLineParams {
private:
  vector<vector<string>> apps_;

public:
  explicit cmdLineParams() = default;
  int process_argv(char** argv);
  inline int getAppCount() const { return apps_.size(); } 
  inline vector<string> getAppParams(int index) const { return apps_[index]; } 
  inline int getAppParamsCount(int index) const { return apps_[index].size(); } 
};

#endif
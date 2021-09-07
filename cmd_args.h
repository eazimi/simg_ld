#ifndef CMD_ARGS_H
#define CMD_ARGS_H

#include <vector>
#include <string>

using namespace std;

class cmd_args {
private:
  char* ld_name_;
  int param_index_;
  vector<int> param_count_;

  int num_of_processes_;
  vector<pair<string, vector<string>>> args_;

public:
  explicit cmd_args() = default;

  inline void set_args(char* ld_name, int param_index, vector<int> param_count)      
  {
    ld_name_ = ld_name;
    param_index_ = param_index;
    param_count_ = param_count;
  }
  
  inline char* ld_name() const { return ld_name_; }
  inline int param_index() const { return param_index_; }
  inline int param_count(int index) const
  {
    auto param_cnt = (index >= 0 && index < param_count_.size()) ? param_count_[index] : -1;
    return param_cnt;
  }

  inline vector<pair<string, vector<string>>> get_processed_args() const { return args_; }
  void process_argv(char** argv);
  inline int get_process_count() const { return num_of_processes_; }
};

#endif
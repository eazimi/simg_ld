#ifndef CMD_ARGS_H
#define CMD_ARGS_H

#include <vector>

using namespace std;

class CMD_Args {
private:
  char* ld_name_;
  int param_index_;
  vector<int> param_count_;

public:
  explicit CMD_Args() = default;

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
};

#endif
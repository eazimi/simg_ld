#ifndef RTLD_H
#define RTLD_H

#include "ld.h"
#include "user_space.h"
#include "cmdline_params.h"
#include "sync_proc.hpp"

using namespace std;

class RTLD {
private:
  unique_ptr<cmdline_params> cmdline_params_;
  unique_ptr<LD> ld_;
  unique_ptr<user_space> vm_;
  unique_ptr<SyncProc> sync_proc_;
  // template<typename T>
  // void run_rtld(int param_index, int param_count, T p);
  void runApp(int socket, int paramsCount);

public:
  explicit RTLD();
  void runAll(char** argv);
};

#endif
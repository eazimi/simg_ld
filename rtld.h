#ifndef RTLD_H
#define RTLD_H

#include "app_loader.h"
#include "cmdline_params.h"
#include "sync_proc.hpp"

using namespace std;

class RTLD {
private:
  std::list<int> allSockets;
  std::list<pid_t> allApps;
  unique_ptr<cmdline_params> cmdline_params_;
  unique_ptr<AppLoader> ld_;
  unique_ptr<SyncProc> sync_proc_;
  // template<typename T>
  // void run_rtld(int param_index, int param_count, T p);
  void runApp(int socket, int paramsCount);
  void handle_waitpid();

public:
  explicit RTLD();
  void runAll(char** argv);
};

#endif
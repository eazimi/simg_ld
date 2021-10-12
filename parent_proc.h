#ifndef PARENTPROC_H
#define PARENTPROC_H

#include "app_loader.h"
#include "cmdline_params.h"
#include "sync_proc.hpp"

using namespace std;

class ParentProc {
private:
  std::list<int> allSockets;
  std::list<pid_t> allApps;
  unique_ptr<cmdLineParams> cmdLineParams_;
  unique_ptr<AppLoader> appLoader_;
  unique_ptr<SyncProc> syncProc_;
  void handle_waitpid();

public:
  explicit ParentProc();
  void run(char** argv);
};

#endif
#ifndef MC_H
#define MC_H

#include "app_loader.h"
#include "cmdline_params.h"
#include "sync_proc.hpp"

using namespace std;

class MC {
private:
  std::list<int> allSockets;
  std::list<pid_t> allApps;
  unique_ptr<cmdLineParams> cmdLineParams_;
  unique_ptr<AppLoader> appLoader_;
  unique_ptr<SyncProc> syncProc_;
  void handle_message(int socket, void* buffer);
  void handle_waitpid();

public:
  explicit MC();
  void run(char** argv);
};

#endif
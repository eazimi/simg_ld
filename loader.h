#ifndef LOADER_H
#define LOADER_H

#include <list>
#include "loader_global_funcs.hpp"
#include "user_space.h"
#include "app_loader.h"

using namespace std;

class Loader {
public:
  explicit Loader()
  {
    g_range_ = std::make_unique<MemoryArea_t>();
    args     = make_unique<CMD_Args>();
    vm_ = make_unique<UserSpace>();
    appLoader_ = make_unique<AppLoader>(); 
  }
  void run(const char** argv);
  unique_ptr<CMD_Args> args;

private:
  int init(const char** argv, pair<int, int>& param_count);
  void reserve_memory_region();
  void hide_free_memory_regions();
  void release_reserved_memory_region();
  void handle_waitpid();
  void handle_message(int socket, void* buffer);
  void remove_process(pid_t pid);

  std::unique_ptr<MemoryArea_t> g_range_ = nullptr;
  unique_ptr<SyncProc> sync_proc_;
  std::list<pid_t> procs_;
  std::list<int> sockets_; 
  unique_ptr<UserSpace> vm_;
  unique_ptr<AppLoader> appLoader_;
};

#endif
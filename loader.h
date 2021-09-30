#ifndef LOADER_H
#define LOADER_H

#include <list>
#include "loader_global_funcs.hpp"
#include "user_space.h"

using namespace std;

class Loader {
public:
  explicit Loader()
  {
    g_range_ = std::make_unique<MemoryArea_t>();
    args     = make_unique<CMD_Args>();
    vm_ = make_unique<user_space>();
  }
  void run(const char** argv);
  unique_ptr<CMD_Args> args;

private:
  int init(const char** argv, pair<int, int>& param_count);
  void run_rtld(const char* ldname, int param_index, int param_count, int socket_id = -1);
  Elf64_Addr get_interpreter_entry(const char* ld_name);
  DynObjInfo load_lsdo(const char* ld_name);
  void* load_elf_interpreter(const char* elf_interpreter, DynObjInfo& info);
  unsigned long map_elf_interpreter_load_segment(int fd, Elf64_Ehdr* ehdr, Elf64_Phdr* phdr);
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
  unique_ptr<user_space> vm_;
};

#endif
#ifndef APP_LOADER_H
#define APP_LOADER_H

#include "dyn_obj_info.hpp"
#include "user_space.h"
#include <elf.h>
#include <memory>

using namespace std;

class AppLoader {
private:
  unique_ptr<user_space> user_space_;
  Elf64_Addr get_interpreter_entry(const char* ld_name);
  unsigned long loadSegment(void* startAddr, int fd, Elf64_Ehdr* ehdr, Elf64_Phdr* phdr);
  void* loadInterpreter(void* startAddr, const char* elf_interpreter, DynObjInfo& info);
  DynObjInfo load_lsdo(void* startAddr, const char* ld_name);

public:
  explicit AppLoader();
  void runRtld(int param_index, int param_count, int socket_id);
  inline void ReserveMemSpace(unsigned long size) const { user_space_->reserve_mem_space(size); }
  inline void* getReservedSpaceStartAddr() const { return user_space_->getStartAddr(); }
  inline unsigned long getReservedSpaceSize() const { return user_space_->getSize(); }
};

#endif
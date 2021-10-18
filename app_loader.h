#ifndef APP_LOADER_H
#define APP_LOADER_H

#include "dyn_obj_info.hpp"
#include "user_space.h"
#include <elf.h>
#include <memory>

using namespace std;

class AppLoader {
private:
  unique_ptr<UserSpace> userSpace_;
  unique_ptr<Stack> stack_;
  unique_ptr<Heap> heap_;
  Elf64_Addr get_interpreter_entry(const char* ld_name);
  unsigned long loadSegment(void* startAddr, int fd, Elf64_Ehdr* ehdr, Elf64_Phdr* phdr);
  void* loadInterpreter(void* startAddr, const char* elf_interpreter, DynObjInfo& info);
  DynObjInfo load_lsdo(void* startAddr, const char* ld_name);

public:
  explicit AppLoader();
  void runRtld(void* loadAddr, vector<string> app_params, int socket_id);
  void runRtld(void* loadAddr, void* lowerHalfAddr);

  inline void* reserveMemSpace(unsigned long relativeDistFromStack, unsigned long size) const
  {
    return userSpace_->reserve_mem_space(relativeDistFromStack, size);
  }

  inline void* reserveMemSpace(void* addr, size_t len) const { return userSpace_->reserve_mem_space(addr, len); }

  inline void* getStackAddr() const
  {
    return userSpace_->get_stack_addr();
  }
};

#endif
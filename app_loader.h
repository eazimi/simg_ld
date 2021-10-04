#ifndef APP_LOADER_H
#define APP_LOADER_H

#include <elf.h>
#include "dyn_obj_info.hpp"
class AppLoader {
private:
  Elf64_Addr get_interpreter_entry(const char* ld_name);  
  unsigned long loadSegment(void* startAddr, int fd, Elf64_Ehdr* ehdr, Elf64_Phdr* phdr);
  void* loadInterpreter(void* startAddr, const char* elf_interpreter, DynObjInfo& info);

public:
  explicit AppLoader() = default;
  DynObjInfo load_lsdo(void* startAddr, const char* ld_name);
};

#endif
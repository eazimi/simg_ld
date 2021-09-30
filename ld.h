#ifndef LD_H
#define LD_H

#include <elf.h>
#include "dyn_obj_info.hpp"
class LD {
private:
  Elf64_Addr get_interpreter_entry(const char* ld_name);
  unsigned long map_elf_interpreter_load_segment(void* startAddr, int fd, Elf64_Ehdr* ehdr, Elf64_Phdr* phdr);
  void* load_elf_interpreter(void* startAddr, const char* elf_interpreter, DynObjInfo& info);

public:
  explicit LD() = default;
};

#endif
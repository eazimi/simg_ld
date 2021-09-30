#ifndef LD_H
#define LD_H

#include "elf.h"

class LD {
private:
  Elf64_Addr get_interpreter_entry(const char* ld_name);
  unsigned long map_elf_interpreter_load_segment(void* startAddr, int fd, Elf64_Ehdr* ehdr, Elf64_Phdr* phdr);

public:
  explicit LD() = default;
};

#endif
#ifndef LD_H
#define LD_H

#include "elf.h"

class LD {
private:
  Elf64_Addr get_interpreter_entry(const char* ld_name);

public:
  explicit LD() = default;
};

#endif
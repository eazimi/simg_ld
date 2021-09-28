#ifndef STACK_H
#define STACH_H

#include "global.hpp"

class Stack {
private:
  void* stack_end_ = nullptr;
  void getProcStatField(enum Procstat_t type, char* out, size_t len);
  Area&& getStackRegion();
  void* get_argc_addr(const void* stackEnd) const;
  void* get_argv_addr(const void* stackEnd) const;
  void* get_env_addr(char** argv, int argc) const;
  ElfW(auxv_t) * get_auxv_addr(const char** env) const;
  void patchAuxv(ElfW(auxv_t) * av, unsigned long phnum, unsigned long phdr, unsigned long entry) const;

public:
  explicit Stack();
  inline void* getStackEnd() const { return stack_end_; }
};

#endif
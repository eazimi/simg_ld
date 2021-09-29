#ifndef STACK_H
#define STACH_H

#include "global.hpp"
#include "dyn_obj_info.hpp"

using namespace std;

class Stack {
private:
  void* stack_end_ = nullptr;
  void getProcStatField(enum Procstat_t type, char* out, size_t len);
  void getStackRegion(Area* stack);
  void* getArgcAddr(const void* stackEnd) const;
  void* getArgvAddr(const void* stackEnd) const;
  void* getEnvAddr(char** argv, int argc) const;
  ElfW(auxv_t) * getAuxvAddr(const char** env) const;
  void patchAuxv(ElfW(auxv_t) * av, unsigned long phnum, unsigned long phdr, unsigned long entry) const;
  void* deepCopyStack(void* newStack, const void* origStack, size_t len, const void* newStackEnd,
                           const void* origStackEnd, const DynObjInfo& info, int param_index, int param_count,
                           int socket_id) const;

public:
  explicit Stack();
  inline void* getStackEnd() const { return stack_end_; }
  void* createNewStack(const DynObjInfo& info, void* stackStartAddr, int param_index, int param_count, int socket_id);
};

#endif
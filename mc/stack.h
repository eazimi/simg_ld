#ifndef STACK_H
#define STACH_H

#include "global.hpp"
#include "dyn_obj_info.hpp"

using namespace std;

class Stack {
private:
  void getProcStatField(enum Procstat_t type, char* out, size_t len);
  void getStackRegion(Area* stack);
  void* getArgcAddr(const void* stackEnd) const;
  void* getArgvAddr(const void* stackEnd) const;
  void* getEnvAddr(char** argv, int argc) const;
  ElfW(auxv_t) * getAuxvAddr(const char** env) const;
  void patchAuxv(ElfW(auxv_t) * av, unsigned long phnum, unsigned long phdr, unsigned long entry) const;
  void* deepCopyStack(void* newStack, const void* origStack, size_t len, const void* newStackEnd,
                           const void* origStackEnd, const DynObjInfo& info, vector<string> app_params,
                           int socket_id) const;
  void* deepCopyStack(void* newStack, const void* origStack, size_t len, const void* newStackEnd,
                           const void* origStackEnd, const DynObjInfo& info) const;

public:
  explicit Stack();
  void* createNewStack(const DynObjInfo& info, void* stackStartAddr, vector<string> app_params, int socket_id);
  void* createNewStack(const DynObjInfo& info, void* stackStartAddr);
};

#endif
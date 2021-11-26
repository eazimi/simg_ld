#ifndef TRAMPOLINE_WRAPPERS_HPP
#define TRAMPOLINE_WRAPPERS_HPP

#include "global.hpp"
#include "switch_context.h"

unsigned long lhFsAddr;

static void* mmapTrampoline(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
  cout << "mmapTrampoline called ..." << endl;
  length    = ROUND_UP(length);
  void* ret = mmap(addr, length, prot, flags, fd, offset);
  return ret;

  // cout << "mmapTrampoline called ..." << endl;
  // void* ret = nullptr;
  // length    = ROUND_UP(length);
  // JUMP_TO_LOWER_HALF(lhFsAddr);
  // ret = mmap(addr, length, prot, flags, fd, offset);
  // RETURN_TO_UPPER_HALF();
  // return ret;
}

#endif
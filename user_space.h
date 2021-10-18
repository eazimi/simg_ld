#ifndef USER_SPACE_H
#define USER_SPACE_H

#include "heap.hpp"
#include "stack.h"

using namespace std;

class UserSpace {
public:
  explicit UserSpace() = default;
  void* reserve_mem_space(unsigned long relativeDistFromStack, unsigned long size) const;
  inline void* reserve_mem_space(void* addr, size_t len) const
  {
    return mmap(addr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
  }
  inline int release_mem_space(void* addr, size_t len) const { return munmap(addr, len); }
  void* get_stack_addr() const;
  void mmap_all_free_spaces();
};

#endif
#ifndef USER_SPACE_H
#define USER_SPACE_H

#include "heap.hpp"
#include "stack.h"

using namespace std;

class UserSpace {
public:
  explicit UserSpace() = default;
  void* reserve_mem_space(unsigned long relativeDistFromStack, unsigned long size);
  void mmap_all_free_spaces();
};

#endif
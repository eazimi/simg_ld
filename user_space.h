#ifndef USER_SPACE_H
#define USER_SPACE_H

#include "heap.hpp"
#include "stack.h"

using namespace std;

class UserSpace {
private:
  void* startAddr_ = nullptr;
  unsigned long size_ = 0;

public:
  explicit UserSpace() = default;
  void reserve_mem_space(unsigned long relativeDistFromStack, unsigned long size);
  void mmap_all_free_spaces();  
  inline void* getStartAddr() const { return startAddr_; }
  inline unsigned long getSize() const { return size_; }
};

#endif
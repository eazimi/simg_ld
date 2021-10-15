#ifndef USER_SPACE_H
#define USER_SPACE_H

#include "heap.hpp"
#include "stack.h"

using namespace std;

class UserSpace {
private:
  void* upperHalf_addr_ = nullptr;
  void* lowerHalf_addr_ = nullptr;

public:
  explicit UserSpace() = default;
  void* reserve_mem_space(unsigned long relativeDistFromStack, unsigned long size);
  void mmap_all_free_spaces();

  inline void *getUpperHalfAddr() const { return upperHalf_addr_; }  
  inline void *getLowerHalfAddr() const { return lowerHalf_addr_; }  
  inline void setUpperHalfAddr(void* addr) { upperHalf_addr_ = addr; }  
  inline void setLowerHalfAddr(void* addr) { lowerHalf_addr_ = addr; }
};

#endif
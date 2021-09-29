#ifndef USER_SPACE_H
#define USER_SPACE_H

#include <memory>
#include "heap.hpp"
#include "stack.h"

using namespace std;

class user_space {
private:
  unique_ptr<Stack> stack_;
  unique_ptr<Heap> heap_;
  void* startAddr_ = nullptr;
  unsigned long size_ = 0;

public:
  explicit user_space();
  void* getStackEnd() const;
  void* createNewHeap(void* heapStartAddr) const;
  void* createNewStack(const DynObjInfo& info, void* stackStartAddr, int param_index, int param_count,
                       int socket_id) const;
  void reserve_mem_space(unsigned long size);
  void mmap_all_free_spaces();  
  inline void* getStartAddr() const { return startAddr_; }
  inline unsigned long getSize() const { return size_; }
};

#endif
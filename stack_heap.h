#ifndef MEM_SPACE_H
#define MEM_SPACE_H

#include <memory>
#include "heap.hpp"
#include "stack.h"

using namespace std;

class stack_heap {
private:
  unique_ptr<Stack> stack_;
  unique_ptr<Heap> heap_;

public:
  explicit stack_heap();
  void* getStackEnd() const;
  void* createNewHeap(void* heapStartAddr) const;
  void* createNewStack(const DynObjInfo& info, void* stackStartAddr, int param_index, int param_count,
                       int socket_id) const;
};

#endif
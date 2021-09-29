#include "stack_heap.h"

stack_heap::stack_heap()
{
  stack_ = make_unique<Stack>();
  heap_  = make_unique<Heap>();
}

inline void* stack_heap::getStackEnd() const
{
  return stack_->getStackEnd();
}

void* stack_heap::createNewHeap(void* heapStartAddr) const
{
  return heap_->createNewHeap(heapStartAddr);
}

void* stack_heap::createNewStack(const DynObjInfo& info, void* stackStartAddr, int param_index, int param_count,
                                int socket_id) const
{
  return stack_->createNewStack(info, stackStartAddr, param_index, param_count, socket_id);
}

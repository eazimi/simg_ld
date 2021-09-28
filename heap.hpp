#ifndef HEAP_HPP
#define HEAP_HPP

#include "global.hpp"

class Heap {
private:
  uint64_t heapSize_ = 0;
  void* heapStartAddr_ = nullptr;

public:
  explicit Heap() = default;
  void* createNewHeap(void* startAddr)
  {
    heapSize_ = 100 * PAGE_SIZE;

    // We go through the mmap wrapper function to ensure that this gets added
    // to the list of upper half regions to be checkpointed.

    void* heapStartAddr = (void*)((unsigned long)startAddr + _1500_MB);

    void* addr = mmapWrapper(heapStartAddr /*0*/, heapSize_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (addr == MAP_FAILED) {
      DLOG(ERROR, "Failed to mmap region. Error: %s\n", strerror(errno));
      return NULL;
    }
    // Add a guard page before the start of heap; this protects
    // the heap from getting merged with a "previous" region.
    mprotect(addr, PAGE_SIZE, PROT_NONE);
    heapStartAddr_ = addr;
    return addr;
  }
};

#endif
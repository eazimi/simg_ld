#ifndef MEM_SPACE_H
#define MEM_SPACE_H

#include <memory>
#include "stack.h"
#include "heap.hpp"

using namespace std;

class mem_space {
private:
  void* start_addr_;
  void* end_addr_;
  unique_ptr<Stack> stack_;
  unique_ptr<Heap> heap_;

public:
  explicit mem_space(void* start, void* end);
  inline void* getStackEnd() const { return stack_->getStackEnd(); }
};

#endif
#ifndef STACK_H
#define STACH_H

#include "global.hpp"

class Stack {
private:
  void* stack_end_ = nullptr;
  Area&& getStackRegion();

public:
  explicit Stack();
  inline void* getStackEnd() const { return stack_end_; }
};

#endif
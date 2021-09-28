#ifndef STACK_H
#define STACH_H

#include "global.hpp"

class Stack {
private:
  void* stack_end_ = nullptr;
  void getProcStatField(enum Procstat_t type, char* out, size_t len);
  Area&& getStackRegion();

public:
  explicit Stack();
  inline void* getStackEnd() const { return stack_end_; }
};

#endif
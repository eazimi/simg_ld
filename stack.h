#ifndef STACK_H
#define STACH_H

class Stack {
private:
  void* stack_end_ = nullptr;

public:
  explicit Stack();
  inline void* getStackEnd() const { return stack_end_; }
};

#endif
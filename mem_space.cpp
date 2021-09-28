#include "mem_space.h"

mem_space::mem_space(void* start, void* end) : start_addr_(start), end_addr_(end)
{
  stack_ = make_unique<Stack>();
  heap_  = make_unique<Heap>();
}
#ifndef TRAMPOLINE_HPP
#define TRAMPOLINE_HPP

#include <string>

using namespace std;

class Trampoline {
public:
  explicit Trampoline() = default;
  off_t getSymbolOffset(int fd, string ldname, string symbol);
  int insertTrampoline(void*, void*);
};

#endif
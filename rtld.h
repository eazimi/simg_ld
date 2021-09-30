#ifndef RTLD_H
#define RTLD_H

#include "ld.h"
#include "user_space.h"

using namespace std;

class RTLD {
private:
  unique_ptr<LD> ld_;
  unique_ptr<user_space> vm_;

public:
  explicit RTLD();
  void run();
};

#endif
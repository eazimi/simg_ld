#include "rtld.h"

RTLD::RTLD()
{
  vm_ = make_unique<user_space>();
  ld_ = make_unique<LD>();
}

void RTLD::run() {}
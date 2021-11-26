#include "switch_context.h"
#include <asm/prctl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

SwitchContext::SwitchContext(unsigned long lowerHalfFs)
{
  this->lowerHalfFs = lowerHalfFs;
  int rc            = syscall(SYS_arch_prctl, ARCH_GET_FS, &this->upperHalfFs);
  if (rc < 0) {
    printf("failed to get fs: %d\n", errno);
    exit(-1);
  }
  this->jumped = 0;
  if (lowerHalfFs > 0 && lowerHalfFs != this->upperHalfFs) {
    rc = syscall(SYS_arch_prctl, ARCH_SET_FS, this->lowerHalfFs);
    if (rc < 0) {
      printf("failed to get fs: %d\n", errno);
      exit(-1);
    }
    this->jumped = 1;
  }
}

SwitchContext::~SwitchContext()
{
  if (this->jumped) {
    int rc = syscall(SYS_arch_prctl, ARCH_SET_FS, this->upperHalfFs);
    if (rc < 0) {
      printf("failed to get fs: %d\n", errno);
      exit(-1);
    }
  }
}
#ifndef WRAPPER_CPP
#define WRAPPER_CPP

#include <dlfcn.h>
#include <iostream>
#include <sys/mman.h>

using namespace std;

typedef void* (*real_mmap_t)(void*, size_t, int, int, int, off_t);

void* real_mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
  return ((real_mmap_t)dlsym(RTLD_NEXT, "mmap"))(addr, length, prot, flags, fd, offset);
}

void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
  puts("libwrapper, mmap()"); 
  return real_mmap(addr, length, prot, flags, fd, offset);

  // cout << "libwrapper, mmap()" << endl;
  // cout << "requested addr: " << std::hex << addr << endl;
  // void *maddr = (void*)((unsigned long)addr + (1000 * 0x1000LL));
  // return real_mmap(maddr, length, prot, flags, fd, offset);
}

#endif
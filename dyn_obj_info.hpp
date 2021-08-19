#ifndef DYN_OBJ_INFO_HPP
#define DYN_OBJ_INFO_HPP

#include <iostream>

class DynObjInfo {
public:
  DynObjInfo() = default;
  DynObjInfo(void* base_addr, void* entry_point) : baseAddr(base_addr), entryPoint(entry_point) {}

  inline void set_base_addr(void* base_addr) { baseAddr = base_addr; }
  inline void* get_base_addr() const { return baseAddr; }

  inline void set_entry_point(void* entry_point) { entryPoint = entry_point; }
  // This function returns the entry point of the ld.so executable given
  // the library handle
  inline void* get_entry_point() const { return entryPoint; }

  inline void set_phnum(uint64_t phnum) { this->phnum = phnum; }
  inline uint64_t get_phnum() const { return phnum; }

  inline void set_phdr(void* phdr) { this->phdr = phdr; }
  inline void* get_phdr() const { return phdr; }

  inline void set_mmap_addr(void* mmap_addr) { mmapAddr = mmap_addr; }
  inline void* get_mmap_addr() const { return mmapAddr; }

  inline void set_sbrk_addr(void* sbrk_addr) { sbrkAddr = sbrk_addr; }
  inline void* get_sbrk_addr() const { return sbrkAddr; }

private:
  void* baseAddr;
  void* entryPoint;
  uint64_t phnum;
  void* phdr;
  void* mmapAddr;
  void* sbrkAddr;
};

#endif

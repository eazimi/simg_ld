#ifndef LOADER_H
#define LOADER_H

#include <elf.h>
#include <stdio.h>

typedef struct __DynObjInfo
{
  void *baseAddr;
  void *entryPoint;
  uint64_t phnum;
  void *phdr;
  void *mmapAddr;
  void *sbrkAddr;
} DynObjInfo_t;

typedef struct __LowerHalfInfo
{
  void *lhSbrk;
  void *lhMmap;
  void *lhMunmap;
  void *lhDlsym;
  unsigned long lhFsAddr;
  void *lhMmapListFptr;
  void *uhEndofHeapFptr;
  void *lhGetDeviceHeapFptr;
  void *lhCopyToCudaPtrFptr;
  void *lhDeviceHeap;
  void *getFatCubinHandle;
} LowerHalfInfo_t;

class Loader
{
public:
    explicit Loader() = default;
    void runRtld(int argc, char** argv);

private:    
    void get_elf_interpreter(char* name, Elf64_Addr *cmd_entry, char *elf_interpreter, void *ld_so_addr);
    DynObjInfo_t safeLoadLib(const char *name);
    void *load_elf_interpreter(int fd, const char *elf_interpreter, Elf64_Addr *ld_so_entry, void *ld_so_addr,
                               DynObjInfo_t *info);
    void *map_elf_interpreter_load_segment(int fd, Elf64_Phdr phdr, void *ld_so_addr);
    void *mmapWrapper(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
    int setupLowerHalfInfo();
    int writeLhInfoToFile();
    
    LowerHalfInfo_t lhInfo;
};

#endif
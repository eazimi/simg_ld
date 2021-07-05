#ifndef LOADER_H
#define LOADER_H

#include <elf.h>
#include <stdio.h>
#include <link.h>
#include <vector>
#include <memory>

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

// Based on the entries in /proc/<pid>/stat as described in `man 5 proc`
enum Procstat_t
{
  PID = 1,
  COMM,  // 2
  STATE, // 3
  PPID,  // 4
  NUM_THREADS = 19,
  STARTSTACK = 27,
};

typedef char *VA; /* VA = virtual address */

#define FILENAMESIZE 1024

typedef union ProcMapsArea
{
  struct
  {
    union
    {
      VA addr; // args required for mmap to restore memory area
      uint64_t __addr;
    };
    union
    {
      VA endAddr; // args required for mmap to restore memory area
      uint64_t __endAddr;
    };
    union
    {
      size_t size;
      uint64_t __size;
    };
    union
    {
      off_t offset;
      uint64_t __offset;
    };
    union
    {
      int prot;
      uint64_t __prot;
    };
    union
    {
      int flags;
      uint64_t __flags;
    };
    union
    {
      unsigned int long devmajor;
      uint64_t __devmajor;
    };
    union
    {
      unsigned int long devminor;
      uint64_t __devminor;
    };
    union
    {
      ino_t inodenum;
      uint64_t __inodenum;
    };

    uint64_t properties;

    char name[FILENAMESIZE];
  };
  char _padding[4096];
} ProcMapsArea;

typedef ProcMapsArea Area;

typedef struct __MmapInfo
{
  void *addr;
  size_t len;
} MmapInfo_t;

typedef struct __MemRange
{
  void *start;
  void *end;
} MemRange_t;

using namespace std;

class Loader
{
public:
    explicit Loader() { g_range = std::make_unique<MemRange_t>(); }
    int init(const char **argv, pair<int, int> &param_count);
    void run(int param_index, const pair<int, int> &param_count);    

private:
    void runRtld(const char* ldname, int param_index, int param_count);
    void get_interpreter_entry(const char *ld_name, Elf64_Addr *cmd_entry);
    DynObjInfo_t safeLoadLib(const char *ld_name);
    void* load_elf_interpreter(const char *elf_interpreter, DynObjInfo_t *info);
    unsigned long map_elf_interpreter_load_segment(int fd, Elf64_Ehdr *ehdr, Elf64_Phdr *phdr);
    void *mmapWrapper(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
    void* sbrkWrapper(intptr_t increment);
    int writeLhInfoToFile();
    void * createNewStackForRtld(const DynObjInfo_t *info, int param_index, int param_count);
    void getStackRegion(Area *stack);
    int readMapsLine(int mapsfd, Area *area);
    char readChar(int fd);
    char readDec(int fd, VA *value);
    char readHex(int fd, VA *virt_mem_addr);
    void getProcStatField(enum Procstat_t type, char *out, size_t len);
    void* deepCopyStack(void *newStack, const void *origStack, size_t len,
              const void *newStackEnd, const void *origStackEnd, const DynObjInfo_t *info, int param_index, int param_count);
    ElfW(auxv_t)* GET_AUXV_ADDR(const char **env);
    void* GET_ENV_ADDR(char **argv, int argc);
    void* GET_ARGV_ADDR(const void* stackEnd);
    void* GET_ARGC_ADDR(const void* stackEnd);
    void patchAuxv(ElfW(auxv_t) *av, unsigned long phnum, unsigned long phdr, unsigned long entry);
    void* createNewHeapForRtld(const DynObjInfo_t *info);
    void addRegionToMMaps(void *addr, size_t length);
    void* __sbrkWrapper(intptr_t increment);
    void* getEntryPoint(DynObjInfo_t info);
    unsigned long getStackPtr();
    void reserveMemRegion();
    void lockFreeMemRegions();
    void unlockReservedMemRegion();
    void setReservedMemRange();
    void printMappedAreas();
    
    LowerHalfInfo_t lhInfo;
    void *__curbrk;
    void *__endOfHeap = 0;
    std::vector<MmapInfo_t> mmaps {};
    std::unique_ptr<MemRange_t> g_range = nullptr;
    std::vector<MemRange_t> mmaps_range {};
    int processCommandLineArgs(const char **argv, pair<int, int> &param_count) const;
};

#endif
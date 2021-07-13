#ifndef LOADER_H
#define LOADER_H

#include <elf.h>
#include <stdio.h>
#include <link.h>
#include <vector>
#include <memory>
#include <stdio.h>
#include "dyn_obj_info.hpp"
#include "global.hpp"
#include "channel.hpp"

using namespace std;

class Loader
{
public:
    explicit Loader() { g_range = std::make_unique<MemoryArea_t>(); }
    int init(const char **argv, pair<int, int> &param_count);
    void run(int param_index, const pair<int, int> &param_count);    

private:
    void runRtld(const char* ldname, int param_index, int param_count);
    Elf64_Addr getInterpreterEntry(const char *ld_name);
    DynObjInfo safeLoadLib(const char *ld_name);
    void* load_elf_interpreter(const char *elf_interpreter, DynObjInfo &info);
    unsigned long map_elf_interpreter_load_segment(int fd, Elf64_Ehdr *ehdr, Elf64_Phdr *phdr);
    void *mmapWrapper(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
    void *createNewStackForRtld(const DynObjInfo &info, int param_index, int param_count);
    void getStackRegion(Area *stack);
    int readMapsLine(int mapsfd, Area *area);
    void getProcStatField(enum Procstat_t type, char *out, size_t len);
    void* deepCopyStack(void *newStack, const void *origStack, size_t len,
              const void *newStackEnd, const void *origStackEnd, const DynObjInfo &info, int param_index, int param_count);
    void* createNewHeapForRtld();
    void reserveMemRegion();
    void lockFreeMemRegions();
    void unlockReservedMemRegion();
    void printMappedAreas();
    
    std::unique_ptr<MemoryArea_t> g_range = nullptr;
    int processCommandLineArgs(const char **argv, pair<int, int> &param_count) const;

    std::unique_ptr<Channel> channel;
};

#endif
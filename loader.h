#ifndef LOADER_H
#define LOADER_H

#include "loader_global_funcs.hpp"

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
    void *createNewStackForRtld(const DynObjInfo &info, int param_index, int param_count);
    void* createNewHeapForRtld();
    void reserve_memeory_region();
    void hide_free_memoty_regions();
    void release_reserved_memory_region();
    
    std::unique_ptr<MemoryArea_t> g_range = nullptr;
    int processCommandLineArgs(const char **argv, pair<int, int> &param_count) const;

    std::unique_ptr<Channel> channel;
};

#endif
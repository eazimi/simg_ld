#ifndef LOADER_GLOBAL_HPP
#define LOADER_GLOBAL_HPP

// Based on the entries in /proc/<pid>/stat as described in `man 5 proc`
enum Procstat_t
{
    PID = 1,
    COMM,  // 2
    STATE, // 3
    PPID,  // 4
    NUM_THREADS = 19,
    STARTSTACK = 27
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

typedef struct __MemRange
{
    void *start;
    void *end;
} MemRange_t;

#endif
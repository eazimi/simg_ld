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

#define MAX_ELF_INTERP_SZ 256

// Logging levels
#define NOISE 3 // Noise!
#define INFO 2  // Informational logs
#define ERROR 1 // Highest error/exception level

#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"
#define KBLU "\x1B[34m"
#define KMAG "\x1B[35m"
#define KCYN "\x1B[36m"
#define KWHT "\x1B[37m"

static const char *colors[] {KNRM, KRED, KBLU, KGRN, KYEL};

#define DLOG(LOG_LEVEL, fmt, ...)                                         \
  do                                                                      \
  {                                                                       \
    fprintf(stderr, "%s[%s +%d]: " fmt KNRM, colors[LOG_LEVEL], __FILE__, \
            __LINE__ __VA_OPT__(, ) __VA_ARGS__);                         \
  } while (0)

#define PAGE_SIZE 0x1000LL
#define _1_GB    0x40000000
#define _1500_MB 0x60000000
#define _3_GB    0xc0000000

// FIXME: 0x1000 is one page; Use sysconf(PAGESIZE) instead.
#define ROUND_DOWN(x) ((unsigned long long)(x) & ~(unsigned long long)(PAGE_SIZE - 1))
#define ROUND_UP(x) (((unsigned long long)(x) + PAGE_SIZE - 1) & \
                     ~(PAGE_SIZE - 1))
#define PAGE_OFFSET(x) ((x) & (PAGE_SIZE - 1))

#define MMAP_OFF_HIGH_MASK ((-(4096ULL << 1) << (8 * sizeof(off_t) - 1)))
#define MMAP_OFF_LOW_MASK (4096ULL - 1)
#define MMAP_OFF_MASK (MMAP_OFF_HIGH_MASK | MMAP_OFF_LOW_MASK)

#define ALIGN (PAGE_SIZE - 1)
#define ROUND_PG(x) (((x) + (ALIGN)) & ~(ALIGN))
#define TRUNC_PG(x) ((x) & ~(ALIGN))
#define PFLAGS(x) ((((x)&PF_R) ? PROT_READ : 0) |  \
				   (((x)&PF_W) ? PROT_WRITE : 0) | \
				   (((x)&PF_X) ? PROT_EXEC : 0))
#define LOAD_ERR ((unsigned long)-1)

// TODO: This is very x86-64 specific; support other architectures??
#define eax rax
#define ebx rbx
#define ecx rcx
#define edx rax
#define ebp rbp
#define esi rsi
#define edi rdi
#define esp rsp
#define CLEAN_FOR_64_BIT_HELPER(args...) #args
#define CLEAN_FOR_64_BIT(args...) CLEAN_FOR_64_BIT_HELPER(args)

#define LD_NAME "/lib64/ld-linux-x86-64.so.2"

static pid_t _parent_pid;

static void pause_run(std::string message)
{
  std::cout << message << std::endl;
  std::cout << "press a key to continue ..." << std::endl;
  std::string str; 
  std::cin >> str;
}

#endif
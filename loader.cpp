#include "loader.h"
#include <assert.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <asm/prctl.h>
#include <syscall.h>
#include <fstream>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <csignal>
#include <sstream>
#include "switch_context.h"
#include "limits.h"

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

const char *colors[] = {KNRM, KRED, KBLU, KGRN, KYEL};

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

// This function returns the entry point of the ld.so executable given
// the library handle
void *Loader::getEntryPoint(DynObjInfo_t info)
{
  return info.entryPoint;
}

// returns the parent's parameters start index in the command line parameters
int Loader::processCommandLineArgs(const char **argv, pair<int, int> &param_count) const
{
  vector<string> argv1, argv2;
  auto *args = &argv1;
  bool seperatorFounded = false;
  auto i {0};
  auto index {0};
  argv++;
  i++;
  while(*argv != nullptr)
  {
    if(strcmp(*argv, (char*)"--") == 0)
    {
      seperatorFounded = (!seperatorFounded) ? true : false;
      if(!seperatorFounded)
        return -1;
      index = i;
      args = &argv2;
    }
    else
      args->push_back(*argv);
    argv++;
    i++;
  }

  // todo: check for more condition, like app's parameter count
  if(!seperatorFounded || argv1.empty() || argv2.empty())
    return -1;
  param_count.first = argv1.size(); // child process
  param_count.second = argv2.size(); // parent process
  return ++index;
}

int Loader::init(const char **argv, pair<int, int> &param_count)
{
  auto param_index = processCommandLineArgs(argv, param_count);
  if(param_index == -1)
  {    
    DLOG(ERROR, "Command line parameters are invalid\n");
    DLOG(ERROR, "Usage: ./simg_ld /PATH/TO/APP1 [APP1_PARAMS] -- /PATH/TO/APP2 [APP2_PARAMS]\n");
    DLOG(ERROR, "exiting ...\n");
    exit(-1);
  }

  _parent_pid = getpid();

  // reserve some 2 GB in the address space, lock remained free areas
  reserveMemRegion();
  lockFreeMemRegions();
  unlockReservedMemRegion();

  return param_index;
}

void Loader::run(int param_index, const pair<int, int> &param_count)
{
  char *ldname = (char *)LD_NAME;

  std::stringstream ss;
  ss << "[PARENT], before fork: getpid() = " << std::dec << getpid();
  pause_run(ss.str());

  pid_t pid = fork();
  assert(pid >= 0);

  if (pid == 0) // child
  {
    sleep(1);
    ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
    runRtld(ldname, 0, param_count.first);
  }
  else // parent
  {
    int status;
    auto wait_ret = waitpid(pid, &status, 0);
    runRtld(ldname, param_index, param_count.second);
  }
}

// This function loads in ld.so, sets up a separate stack for it, and jumps
// to the entry point of ld.so
void Loader::runRtld(const char* ldname, int param_index, int param_count)
{
  int rc = -1;

  // Load RTLD (ld.so)  
  DynObjInfo_t ldso = safeLoadLib(ldname);
  std::cout << ((getpid() == _parent_pid) ? "[PARENT], " : "[CHILD], ") << "lsdo.baseAddr: " 
            << std::hex << ldso.baseAddr << std::endl;

  if (ldso.baseAddr == NULL || ldso.entryPoint == NULL)
  {
    DLOG(ERROR, "Error loading the runtime loader (%s). Exiting...\n", ldname);
    return;
  }

  // Pointer to the ld.so entry point
  void *ldso_entrypoint = getEntryPoint(ldso);

  // Create new stack region to be used by RTLD
  void *newStack = createNewStackForRtld(&ldso, param_index, param_count);
  if (!newStack)
  {
    DLOG(ERROR, "Error creating new stack for RTLD. Exiting...\n");
    exit(-1);
  }

  // Create new heap region to be used by RTLD
  void *newHeap = createNewHeapForRtld(&ldso);
  if (!newHeap)
  {
    DLOG(ERROR, "Error creating new heap for RTLD. Exiting...\n");
    exit(-1);
  }

  std::stringstream ss;
  ss << ((getpid() == _parent_pid) ? "[PARENT], " : "[CHILD], ") 
     << "before jumping to sp: " << std::dec << getpid();  
  pause_run(ss.str());
  printMappedAreas();

  // Change the stack pointer to point to the new stack and jump into ld.so
  asm volatile(CLEAN_FOR_64_BIT(mov %0, %%esp;)
               :
               : "g"(newStack)
               : "memory");
  asm volatile("jmp *%0"
               :
               : "g"(ldso_entrypoint)
               : "memory");
}

// This function allocates a new heap for (the possibly second) ld.so.
// The initial heap size is 1 page
//
// Returns the start address of the new heap on success, or NULL on
// failure.
void *Loader::createNewHeapForRtld(const DynObjInfo_t *info)
{
  const uint64_t heapSize = 100 * PAGE_SIZE;

  // We go through the mmap wrapper function to ensure that this gets added
  // to the list of upper half regions to be checkpointed.

  void *startAddr = (void*)((unsigned long)g_range->start + _1500_MB);

  void *addr = mmapWrapper(startAddr /*0*/, heapSize, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  if (addr == MAP_FAILED)
  {
    DLOG(ERROR, "Failed to mmap region. Error: %s\n",
         strerror(errno));
    return NULL;
  }
  // Add a guard page before the start of heap; this protects
  // the heap from getting merged with a "previous" region.
  mprotect(addr, PAGE_SIZE, PROT_NONE);
  __curbrk = ((void *)((VA)addr + PAGE_SIZE));
  __endOfHeap = (void *)ROUND_UP((void *)((VA)addr + heapSize));
  return addr;
}

/* Read non-null character, return null if EOF */
char Loader::readChar(int fd)
{
  char c;
  int rc;

  do
  {
    rc = read(fd, &c, 1);
  } while (rc == -1 && errno == EINTR);
  if (rc <= 0)
    return 0;
  return c;
}

/* Read decimal number, return value and terminating character */
char Loader::readDec(int fd, VA *value)
{
  char c;
  unsigned long int v = 0;

  while (1)
  {
    c = readChar(fd);
    if ((c >= '0') && (c <= '9'))
      c -= '0';
    else
      break;
    v = v * 10 + c;
  }
  *value = (VA)v;
  return c;
}

/* Read decimal number, return value and terminating character */
char Loader::readHex(int fd, VA *virt_mem_addr)
{
  char c;
  unsigned long int v = 0;

  while (1)
  {
    c = readChar(fd);
    if ((c >= '0') && (c <= '9'))
      c -= '0';
    else if ((c >= 'a') && (c <= 'f'))
      c -= 'a' - 10;
    else if ((c >= 'A') && (c <= 'F'))
      c -= 'A' - 10;
    else
      break;
    v = v * 16 + c;
  }
  *virt_mem_addr = (VA)v;
  return c;
}

int Loader::readMapsLine(int mapsfd, Area *area)
{
  char c, rflag, sflag, wflag, xflag;
  int i;
  off_t offset;
  unsigned int long devmajor, devminor, inodenum;
  VA startaddr, endaddr;

  c = readHex(mapsfd, &startaddr);
  if (c != '-')
  {
    if ((c == 0) && (startaddr == 0))
      return (0);
    goto skipeol;
  }
  c = readHex(mapsfd, &endaddr);
  if (c != ' ')
    goto skipeol;
  if (endaddr < startaddr)
    goto skipeol;

  rflag = c = readChar(mapsfd);
  if ((c != 'r') && (c != '-'))
    goto skipeol;
  wflag = c = readChar(mapsfd);
  if ((c != 'w') && (c != '-'))
    goto skipeol;
  xflag = c = readChar(mapsfd);
  if ((c != 'x') && (c != '-'))
    goto skipeol;
  sflag = c = readChar(mapsfd);
  if ((c != 's') && (c != 'p'))
    goto skipeol;

  c = readChar(mapsfd);
  if (c != ' ')
    goto skipeol;

  c = readHex(mapsfd, (VA *)&offset);
  if (c != ' ')
    goto skipeol;
  area->offset = offset;

  c = readHex(mapsfd, (VA *)&devmajor);
  if (c != ':')
    goto skipeol;
  c = readHex(mapsfd, (VA *)&devminor);
  if (c != ' ')
    goto skipeol;
  c = readDec(mapsfd, (VA *)&inodenum);
  area->name[0] = '\0';
  while (c == ' ')
    c = readChar(mapsfd);
  if (c == '/' || c == '[')
  { /* absolute pathname, or [stack], [vdso], etc. */
    i = 0;
    do
    {
      area->name[i++] = c;
      if (i == sizeof area->name)
        goto skipeol;
      c = readChar(mapsfd);
    } while (c != '\n');
    area->name[i] = '\0';
  }

  if (c != '\n')
    goto skipeol;

  area->addr = startaddr;
  area->endAddr = endaddr;
  area->size = endaddr - startaddr;
  area->prot = 0;
  if (rflag == 'r')
    area->prot |= PROT_READ;
  if (wflag == 'w')
    area->prot |= PROT_WRITE;
  if (xflag == 'x')
    area->prot |= PROT_EXEC;
  area->flags = MAP_FIXED;
  if (sflag == 's')
    area->flags |= MAP_SHARED;
  if (sflag == 'p')
    area->flags |= MAP_PRIVATE;
  if (area->name[0] == '\0')
    area->flags |= MAP_ANONYMOUS;

  area->devmajor = devmajor;
  area->devminor = devminor;
  area->inodenum = inodenum;
  return (1);

skipeol:
  fprintf(stderr, "ERROR: readMapsLine*: bad maps line <%c", c);
  while ((c != '\n') && (c != '\0'))
  {
    c = readChar(mapsfd);
    printf("%c", c);
  }
  printf(">\n");
  abort();
  return 0; /* NOTREACHED : stop compiler warning */
}

// Returns the [stack] area by reading the proc maps
void Loader::getStackRegion(Area *stack) // OUT
{
  Area area;
  int mapsfd = open("/proc/self/maps", O_RDONLY);
  while (readMapsLine(mapsfd, &area))
  {
    if (strstr(area.name, "[stack]") && area.endAddr >= (VA)&area)
    {
      *stack = area;
      break;
    }
  }
  close(mapsfd);
}

// Returns the /proc/self/stat entry in the out string (of length len)
void Loader::getProcStatField(enum Procstat_t type, char *out, size_t len)
{
  const char *procPath = "/proc/self/stat";
  char sbuf[1024] = {0};
  int field_counter = 0;
  char *field_str = nullptr;
  int fd, num_read;

  fd = open(procPath, O_RDONLY);
  if (fd < 0)
  {
    DLOG(ERROR, "Failed to open %s. Error: %s\n", procPath, strerror(errno));
    return;
  }

  num_read = read(fd, sbuf, sizeof sbuf - 1);
  close(fd);
  if (num_read <= 0)
    return;
  sbuf[num_read] = '\0';

  field_str = strtok(sbuf, " ");
  while (field_str && field_counter != type)
  {
    field_str = strtok(nullptr, " ");
    field_counter++;
  }

  if (field_str)
  {
    strncpy(out, field_str, len);
  }
  else
  {
    DLOG(ERROR, "Failed to parse %s.\n", procPath);
  }
}

// This function does three things:
//  1. Creates a new stack region to be used for initialization of RTLD (ld.so)
//  2. Deep copies the original stack (from the kernel) in the new stack region
//  3. Returns a pointer to the beginning of stack in the new stack region
void *Loader::createNewStackForRtld(const DynObjInfo_t *info, int param_index, int param_count)
{
  Area stack;
  char stackEndStr[20] = {0};
  getStackRegion(&stack);

  // 1. Allocate new stack region
  // We go through the mmap wrapper function to ensure that this gets added
  // to the list of upper half regions to be checkpointed.

  void *startAddr = (void*)((unsigned long)g_range->start + _1_GB);

  void *newStack = mmapWrapper(startAddr, stack.size, PROT_READ | PROT_WRITE,
                               MAP_GROWSDOWN | MAP_PRIVATE | MAP_ANONYMOUS,
                               -1, 0);
  if (newStack == MAP_FAILED)
  {
    DLOG(ERROR, "Failed to mmap new stack region: %s\n", strerror(errno));
    return nullptr;
  }
  // DLOG(INFO, "New stack mapped at: %p\n", newStack);

  // 3. Get pointer to the beginning of the stack in the new stack region
  // The idea here is to look at the beginning of stack in the original
  // stack region, and use that to index into the new memory region. The
  // same offsets are valid in both the stack regions.
  getProcStatField(STARTSTACK, stackEndStr, sizeof stackEndStr);

  // NOTE: The kernel sets up the stack in the following format.
  //      -1(%rsp)                       Stack end for application
  //      0(%rsp)                        argc (Stack start for application)
  //      LP_SIZE(%rsp)                  argv[0]
  //      (2*LP_SIZE)(%rsp)              argv[1]
  //      ...
  //      (LP_SIZE*(argc))(%rsp)         NULL
  //      (LP_SIZE*(argc+1))(%rsp)       envp[0]
  //      (LP_SIZE*(argc+2))(%rsp)       envp[1]
  //      ...
  //                                     NULL
  //
  // NOTE: proc-stat returns the address of argc on the stack.
  // argv[0] is 1 LP_SIZE ahead of argc, i.e., startStack + sizeof(void*)
  // Stack End is 1 LP_SIZE behind argc, i.e., startStack - sizeof(void*)
  // sizeof(unsigned long) == sizeof(void*) == 8 on x86-64
  unsigned long origStackEnd = atol(stackEndStr) - sizeof(unsigned long);
  unsigned long origStackOffset = origStackEnd - (unsigned long)stack.addr;
  unsigned long newStackOffset = origStackOffset;
  void *newStackEnd = (void *)((unsigned long)newStack + newStackOffset);

  // printf("origStack: %lu origStackOffset: %lu OrigStackEnd: %lu \n", (unsigned long)stack.addr, (unsigned long)origStackOffset, (unsigned long)origStackEnd);
  // printf("newStack: %lu newStackOffset: %lu newStackEnd: %lu \n", (unsigned long)newStack, (unsigned long)newStackOffset, (unsigned long)newStackEnd);

  // 2. Deep copy stack
  newStackEnd = deepCopyStack(newStack, stack.addr, stack.size,
                              (void *)newStackEnd, (void *)origStackEnd,
                              info, param_index, param_count);

  return newStackEnd;
}

void *Loader::GET_ARGC_ADDR(const void *stackEnd)
{
  return (void *)((uintptr_t)(stackEnd) + sizeof(uintptr_t));
}

// Returns pointer to argv[0], given a pointer to end of stack
void *Loader::GET_ARGV_ADDR(const void *stackEnd)
{
  return (void *)((unsigned long)(stackEnd) + 2 * sizeof(uintptr_t));
}

// Returns pointer to env[0], given a pointer to end of stack
void *Loader::GET_ENV_ADDR(char **argv, int argc)
{
  return (void *)&argv[argc + 1];
}

// Returns a pointer to aux vector, given a pointer to the environ vector
// on the stack
ElfW(auxv_t) * Loader::GET_AUXV_ADDR(const char **env)
{
  ElfW(auxv_t) * auxvec;
  const char **evp = env;
  while (*evp++ != nullptr)
    ;
  auxvec = (ElfW(auxv_t) *)evp;
  return auxvec;
}

// Creates a deep copy of the stack region pointed to be `origStack` at the
// location pointed to be `newStack`. Returns the start-of-stack pointer
// in the new stack region.
void *Loader::deepCopyStack(void *newStack, const void *origStack, size_t len,
                            const void *newStackEnd, const void *origStackEnd,
                            const DynObjInfo_t *info, int param_index, int param_count)
{
  // Return early if any pointer is NULL
  if (!newStack || !origStack ||
      !newStackEnd || !origStackEnd ||
      !info)
  {
    return nullptr;
  }

  // First, we do a shallow copy, which is essentially, just copying the
  // bits from the original stack into the new stack.
  memcpy(newStack, origStack, len);

  // Next, turn the shallow copy into a deep copy.
  //
  // The main thing we need to do is to patch the argv and env vectors in
  // the new stack to point to addresses in the new stack region. Note that
  // the argv and env are simply arrays of pointers. The pointers point to
  // strings in other locations in the stack.

  void *origArgcAddr = (void *)GET_ARGC_ADDR(origStackEnd);
  int origArgc = *(int *)origArgcAddr;
  char **origArgv = (char **)GET_ARGV_ADDR(origStackEnd);
  const char **origEnv = (const char **)GET_ENV_ADDR(origArgv, origArgc);

  void *newArgcAddr = (void *)GET_ARGC_ADDR(newStackEnd);
  int newArgc = *(int *)newArgcAddr;
  char **newArgv = (char **)GET_ARGV_ADDR(newStackEnd);
  const char **newEnv = (const char **)GET_ENV_ADDR(newArgv, newArgc);
  ElfW(auxv_t) *newAuxv = GET_AUXV_ADDR(newEnv);

  // Patch the argv vector in the new stack
  //   First, set up the argv vector based on the original stack
  for (int i = 0; origArgv[i] != nullptr; i++)
  {
    off_t argvDelta = (uintptr_t)origArgv[i] - (uintptr_t)origArgv;
    newArgv[i] = (char *)((uintptr_t)newArgv + (uintptr_t)argvDelta);
  }

  //   Next, we patch argv[0], the first argument, on the new stack
  //   to point to "/path/to/ld.so".
  //
  //   From the point of view of ld.so, it would appear as if it was called
  //   like this: $ /lib/ld.so /path/to/target.exe app-args ...
  //
  //   NOTE: The kernel loader needs to be called with at least two arguments
  //   to get a stack that is 16-byte aligned at the start. Since we want to
  //   be able to jump into ld.so with at least two arguments (ld.so and the
  //   target exe) on the new stack, we also need two arguments on the
  //   original stack.
  //
  //   If the original stack had just one argument, we would have inherited
  //   that alignment in the new stack. Trying to push in another argument
  //   (target exe) on the new stack would destroy the 16-byte alignment
  //   on the new stack. This would lead to a crash later on in ld.so.
  //
  //   The problem is that there are instructions (like, "movaps") in ld.so's
  //   code that operate on the stack memory region and require their
  //   operands to be 16-byte aligned. A non-16-byte-aligned operand (for
  //   example, the stack base pointer) leads to a general protection
  //   exception (#GP), which translates into a segfault for the user
  //   process.
  //
  //   The Linux kernel ensures that the start of stack is always 16-byte
  //   aligned. It seems like this is part of the Linux kernel x86-64 ABI.
  //   For example, see here:
  //
  //     https://elixir.bootlin.com/linux/v4.18.11/source/fs/binfmt_elf.c#L150
  //
  //     https://elixir.bootlin.com/linux/v4.18.11/source/fs/binfmt_elf.c#L288
  //
  //   (The kernel uses the STACK_ROUND macro to first set up the stack base
  //    at a 16-byte aligned address, and then pushes items on the stack.)
  //
  //   We could do something similar on the new stack region. But perhaps it's
  //   easier to just depend on the original stack having at least two args:
  //   "/path/to/kernel-loader" and "/path/to/target.exe".
  //
  //   NOTE: We don't need to patch newArgc, since the original stack,
  //   from where we would have inherited the data in the new stack, already
  //   had the correct value for origArgc. We just make argv[0] in the
  //   new stack to point to "/path/to/ld.so", instead of
  //   "/path/to/kernel-loader".
  // off_t argvDelta = (uintptr_t)getenv("TARGET_LD") - (uintptr_t)origArgv;
  
  // in child parent process 
  if (param_index == 0)
  {
    off_t argvDelta = (uintptr_t)origArgv[1] - (uintptr_t)origArgv;
    newArgv[0] = (char *)((uintptr_t)newArgv + (uintptr_t)argvDelta);
    newArgv[param_count + 1] = nullptr;
    // *(int *)newArgcAddr = param_count + 1;
  } 
  else // in the child process
  {
    // newArgv[0] = (char *)((uintptr_t)newArgv + (uintptr_t)param_index);
    // auto i {1};
    // for(; i<=param_count; i++)
    //   newArgv[i] = (char *)((uintptr_t)newArgv + (uintptr_t)i);
    // newArgv[i] = nullptr;

    newArgv[0] = newArgv[param_index];
    auto i {0};
    for(; i<param_count; i++)
      newArgv[i+1] = newArgv[param_index+i];
    newArgv[i+1] = nullptr;
    // *(int *)newArgcAddr = param_count + 1;
  }
  *(int *)newArgcAddr = param_count + 1;
  
  // newArgv[1] = (char*)"/home";
  // newArgv[1][1] = '\0';

  // newArgv[1][0] = '\0';
  // newArgv[2][0] = '\0';   

  // off_t argvDelta;
  // // if (strcmp(newArgv[1], appName) == 0)
  //   argvDelta = (uintptr_t)origArgv[1] - (uintptr_t)origArgv;
  // // else if (strcmp(newArgv[2], appName) == 0)
  //   // argvDelta = (uintptr_t)origArgv[2] - (uintptr_t)origArgv;
  // // newArgv[0] = (char *)((uintptr_t)newArgv + (uintptr_t)argvDelta);
  // memcpy(newArgv[0], appName, strlen(appName));
  // newArgv[0][strlen(appName)] = '\0';

  // Patch the env vector in the new stack
  for (int i = 0; origEnv[i] != nullptr; i++)
  {
    off_t envDelta = (uintptr_t)origEnv[i] - (uintptr_t)origEnv;
    newEnv[i] = (char *)((uintptr_t)newEnv + (uintptr_t)envDelta);
  }

  // The aux vector, which we would have inherited from the original stack,
  // has entries that correspond to the kernel loader binary. In particular,
  // it has these entries AT_PHNUM, AT_PHDR, and AT_ENTRY that correspond
  // to kernel-loader. So, we atch the aux vector in the new stack to
  // correspond to the new binary: the freshly loaded ld.so.
  patchAuxv(newAuxv, info->phnum,
            (uintptr_t)info->phdr,
            (uintptr_t)info->entryPoint);

  // printf("newArgv[-2]: %lu \n", (unsigned long)&newArgv[0]);

  // We clear out the rest of the new stack region just in case ...
  memset(newStack, 0, (size_t)((uintptr_t)&newArgv[-2] - (uintptr_t)newStack));

  // Return the start of new stack.
  return (void *)newArgcAddr;
}

// Given a pointer to aux vector, parses the aux vector, and patches the
// following three entries: AT_PHDR, AT_ENTRY, and AT_PHNUM
void Loader::patchAuxv(ElfW(auxv_t) * av, unsigned long phnum,
                       unsigned long phdr, unsigned long entry)
{
  for (; av->a_type != AT_NULL; ++av)
  {
    switch (av->a_type)
    {
    case AT_PHNUM:
      av->a_un.a_val = phnum;
      break;
    case AT_PHDR:
      av->a_un.a_val = phdr;
      break;
    case AT_ENTRY:
      av->a_un.a_val = entry;
      break;
    case AT_RANDOM:
      // DLOG(NOISE, "AT_RANDOM value: 0%lx\n", av->a_un.a_val);
      break;
    default:
      break;
    }
  }
}

DynObjInfo_t Loader::safeLoadLib(const char *ld_name)
{
  void *ld_so_addr = NULL;
  DynObjInfo_t info = {0};

  int ld_so_fd;
  Elf64_Addr cmd_entry, ld_so_entry;
  char elf_interpreter[MAX_ELF_INTERP_SZ];

  int cmd_fd = open(ld_name, O_RDONLY);
  get_elf_interpreter(cmd_fd, &cmd_entry, elf_interpreter, ld_so_addr);
  // FIXME: The ELF Format manual says that we could pass the cmd_fd to ld.so,
  //   and it would use that to load it.
  close(cmd_fd);
  strncpy(elf_interpreter, ld_name, sizeof elf_interpreter);

  ld_so_fd = open(elf_interpreter, O_RDONLY);
  assert(ld_so_fd != -1);
  info.baseAddr = load_elf_interpreter(ld_name, &info);

  // FIXME: The ELF Format manual says that we could pass the ld_so_fd to ld.so,
  //   and it would use that to load it.
  close(ld_so_fd);
  info.entryPoint = (void *)((unsigned long)info.baseAddr +
                             (unsigned long)cmd_entry);
  return info;
}

void Loader::unlockReservedMemRegion()
{
  munmap(g_range->start, (unsigned long)g_range->end - (unsigned long)g_range->start);
}

void Loader::lockFreeMemRegions()
{
  // auto mmap_start = (void *)0x10000;
  // auto mmap_end = (void *)0x400000;
  // auto mmap_length = (unsigned long)mmap_end - (unsigned long)mmap_start;
  // void *mmap_ret = mmap(mmap_start, mmap_length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
  // if (mmap_ret == MAP_FAILED)
  // {
  //   DLOG(ERROR, "failed to lock the free spot from 0x1000. %s\n", strerror(errno));
  //   exit(-1);
  // }

  mmaps_range.clear();
  Area area;
  int mapsfd = open("/proc/self/maps", O_RDONLY);
  bool firstLine = true;
  MemRange_t range;
  while (readMapsLine(mapsfd, &area))
  { 
    // todo: check if required to add this condition: (area.endAddr >= (VA)&area)       
    if(firstLine)
    {
      range.start = area.endAddr;
      firstLine = false;
      continue;
    }
    range.end = area.addr;
    mmaps_range.push_back(std::move(range));
    range.start = area.endAddr;    
  }
  close(mapsfd);
  mmaps_range.pop_back();

  auto mmaps_size = mmaps_range.size()-1;
  for (auto i = 0; i <= mmaps_size; i++)
  {
    auto start_mmap = (unsigned long)(mmaps_range[i].start);
    auto length = (unsigned long)(mmaps_range[i].end) - start_mmap;
    if(length == 0)
      continue;
    void *mmap_ret = mmap((void *)start_mmap, length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);    
    if (mmap_ret == MAP_FAILED)
    {
      DLOG(ERROR, "failed to lock the free spot. %s\n", strerror(errno));
      exit(-1);
    }
  }
}

void Loader::reserveMemRegion()
{
  bool lh_initialized = false;
  // proc-stat returns the address of argc on the stack.
  unsigned long argcAddr = getStackPtr();

  // argv[0] is 1 LP_SIZE ahead of argc, i.e., startStack + sizeof(void*)
  // Stack End is 1 LP_SIZE behind argc, i.e., startStack - sizeof(void*)
  void *stack_end = (void *)(argcAddr - sizeof(unsigned long));
  int argc = *(int *)argcAddr;
  char **argv = (char **)(argcAddr + sizeof(unsigned long));
  char **ev = &argv[argc + 1];

  // Copied from glibc source
  ElfW(auxv_t) * auxvec;
  char **evp = ev;
  while (*evp++ != NULL)
    ;
  auxvec = (ElfW(auxv_t) *)evp;

  setReservedMemRange();
  void *region = mmapWrapper(g_range->start, (unsigned long)g_range->end - (unsigned long)g_range->start, PROT_READ | PROT_WRITE,
                             MAP_GROWSDOWN | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (region == MAP_FAILED)
  {
    DLOG(ERROR, "Failed to mmap region: %s\n", strerror(errno));
  }
}

void Loader::printMappedAreas()
{
  std::cout << ((getpid() == _parent_pid) ? "[PARENT], " : "[CHILD], ") << "printing mmaped regions ..." << std::endl;
  std::string maps_path = "/proc/self/maps";
  std::filebuf fb;
  std::string line;
  if(fb.open(maps_path, std::ios_base::in))
  {
    std::istream is(&fb);
    while (std::getline(is, line))
      std::cout << line << std::endl; 
    fb.close();
  }
}

void Loader::setReservedMemRange()
{
  Area area;
  bool found = false;
  int mapsfd = open("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0)
  {
    DLOG(ERROR, "Failed to open proc maps\n");
    return;
  }
  while (readMapsLine(mapsfd, &area))
  {
    if (strstr(area.name, "[stack]"))
    {
      found = true;
      break;
    }
  }
  close(mapsfd);

  if (found)
  {
    g_range->start = (VA)area.addr - _3_GB;
    g_range->end = (VA)area.addr - _1_GB;
  }

  // std::cout << "setReservedMemRange(): start = " << std::hex << g_range->start << " , end = " << g_range->end << std::endl;
}

// Returns the address of argc on the stack
unsigned long Loader::getStackPtr()
{
  // From man 5 proc: See entry for /proc/[pid]/stat
  int pid;
  char cmd[PATH_MAX];
  char state;
  int ppid;
  int pgrp;
  int session;
  int tty_nr;
  int tpgid;
  unsigned flags;
  unsigned long minflt;
  unsigned long cminflt;
  unsigned long majflt;
  unsigned long cmajflt;
  unsigned long utime;
  unsigned long stime;
  long cutime;
  long cstime;
  long priority;
  long nice;
  long num_threads;
  long itrealvalue;
  unsigned long long starttime;
  unsigned long vsize;
  long rss;
  unsigned long rsslim;
  unsigned long startcode;
  unsigned long endcode;
  unsigned long startstack;
  unsigned long kstkesp;
  unsigned long kstkeip;
  unsigned long signal_map;
  unsigned long blocked;
  unsigned long sigignore;
  unsigned long sigcatch;
  unsigned long wchan;
  unsigned long nswap;
  unsigned long cnswap;
  int exit_signal;
  int processor;
  unsigned rt_priority;
  unsigned policy;

  FILE *f = fopen("/proc/self/stat", "r");
  if (f)
  {
    fscanf(f, "%d "
              "%s %c "
              "%d %d %d %d %d "
              "%u "
              "%lu %lu %lu %lu %lu %lu "
              "%ld %ld %ld %ld %ld %ld "
              "%llu "
              "%lu "
              "%ld "
              "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu "
              "%d %d %u %u",
           &pid,
           cmd, &state,
           &ppid, &pgrp, &session, &tty_nr, &tpgid,
           &flags,
           &minflt, &cminflt, &majflt, &cmajflt, &utime, &stime,
           &cutime, &cstime, &priority, &nice, &num_threads, &itrealvalue,
           &starttime,
           &vsize,
           &rss,
           &rsslim, &startcode, &endcode, &startstack, &kstkesp, &kstkeip,
           &signal_map, &blocked, &sigignore, &sigcatch, &wchan, &nswap,
           &cnswap,
           &exit_signal, &processor,
           &rt_priority, &policy);
    fclose(f);
  }
  return startstack;
}

void *Loader::load_elf_interpreter(const char *elf_interpreter , DynObjInfo_t *info)
{
  int ld_so_fd = open(elf_interpreter, O_RDONLY);
  assert(ld_so_fd != -1);

  char e_ident[EI_NIDENT];
  int rc;

  rc = read(ld_so_fd, e_ident, sizeof(e_ident));
  assert(rc == sizeof(e_ident));
  assert(strncmp(e_ident, ELFMAG, sizeof(ELFMAG) - 1) == 0);
  // FIXME:  Add support for 32-bit ELF later
  assert(e_ident[EI_CLASS] == ELFCLASS64);

  // Reset fd to beginning and parse file header
  lseek(ld_so_fd, 0, SEEK_SET);
  Elf64_Ehdr elf_hdr;
  rc = read(ld_so_fd, &elf_hdr, sizeof(elf_hdr));
  assert(rc == sizeof(elf_hdr));

  // Find ELF interpreter
  int phoff = elf_hdr.e_phoff;
  lseek(ld_so_fd, phoff, SEEK_SET);

  Elf64_Phdr *phdr;
  Elf64_Ehdr *ehdr = &elf_hdr;
  ssize_t sz = ehdr->e_phnum * sizeof(Elf64_Phdr);
  phdr = (Elf64_Phdr*) alloca(sz);

  if (read(ld_so_fd, phdr, sz) != sz)
    DLOG(ERROR, "can't read program header");

  unsigned long baseAddr = map_elf_interpreter_load_segment(ld_so_fd, ehdr, phdr);

  info->phnum = elf_hdr.e_phnum;
  info->phdr = (VA)baseAddr + elf_hdr.e_phoff;
  return (void*)baseAddr;
}

unsigned long Loader::map_elf_interpreter_load_segment(int fd, Elf64_Ehdr *ehdr, Elf64_Phdr *phdr)
{
	unsigned long minva, maxva;
	Elf64_Phdr *iter;
	ssize_t sz;
	int flags, dyn = ehdr->e_type == ET_DYN;
	unsigned char *p, *base;

	minva = (unsigned long)-1;
	maxva = 0;

	for (iter = phdr; iter < &phdr[ehdr->e_phnum]; iter++)
	{
		if (iter->p_type != PT_LOAD)
			continue;
		if (iter->p_vaddr < minva)
			minva = iter->p_vaddr;
		if (iter->p_vaddr + iter->p_memsz > maxva)
			maxva = iter->p_vaddr + iter->p_memsz;
	}

	minva = TRUNC_PG(minva);
	maxva = ROUND_PG(maxva);

	/* For dynamic ELF let the kernel chose the address. */
	// hint = dyn ? NULL : (unsigned char *)minva;
	flags = dyn ? 0 : MAP_FIXED;
	flags |= (MAP_PRIVATE | MAP_ANONYMOUS);

	/* Check that we can hold the whole image. */
	// base = (unsigned char*) mmap(hint, maxva - minva, PROT_NONE, flags, -1, 0);
	base = (unsigned char*) mmap(g_range->start, maxva - minva, PROT_NONE, flags, -1, 0);
	if (base == (void *)-1)
		return -1;
	munmap(base, maxva - minva);

  // base = (unsigned char*) g_range->start;

	flags = MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE;
	// flags = MAP_FIXED | MAP_PRIVATE;
	/* Now map each segment separately in precalculated address. */
	for (iter = phdr; iter < &phdr[ehdr->e_phnum]; iter++)
	{
		unsigned long off, start;
		if (iter->p_type != PT_LOAD)
			continue;
		off = iter->p_vaddr & ALIGN;
		start = dyn ? (unsigned long)base : 0;
		// start = (unsigned long)g_range->start;
		start += TRUNC_PG(iter->p_vaddr);
		sz = ROUND_PG(iter->p_memsz + off);

		p = (unsigned char*) mmap((void *)start, sz, PROT_WRITE, flags, -1, 0);
		if (p == (void *)-1)
    {
      munmap(base, maxva - minva);
      return LOAD_ERR;
    }
    if (lseek(fd, iter->p_offset, SEEK_SET) < 0)
    {
      munmap(base, maxva - minva);
      return LOAD_ERR;
    }
		if (read(fd, p + off, iter->p_filesz) !=
			(ssize_t)iter->p_filesz)
    {
      munmap(base, maxva - minva);
      return LOAD_ERR;
    }
		mprotect(p, sz, PFLAGS(iter->p_flags));
	}

	return (unsigned long)base;
}

void Loader::get_elf_interpreter(int fd, Elf64_Addr *cmd_entry, char *elf_interpreter, void *ld_so_addr)
{
  int rc;
  char e_ident[EI_NIDENT];

  rc = read(fd, e_ident, sizeof(e_ident));
  assert(rc == sizeof(e_ident));
  assert(strncmp(e_ident, ELFMAG, strlen(ELFMAG)) == 0);
  assert(e_ident[EI_CLASS] == ELFCLASS64); // FIXME:  Add support for 32-bit ELF

  // Reset fd to beginning and parse file header
  lseek(fd, 0, SEEK_SET);
  Elf64_Ehdr elf_hdr;
  rc = read(fd, &elf_hdr, sizeof(elf_hdr));
  assert(rc == sizeof(elf_hdr));
  *cmd_entry = elf_hdr.e_entry;
}

void *Loader::mmapWrapper(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
  void *ret = MAP_FAILED;
  JUMP_TO_LOWER_HALF(lhInfo.lhFsAddr);
  if (offset & MMAP_OFF_MASK)
  {
    errno = EINVAL;
    return ret;
  }
  length = ROUND_UP(length);
  ret = mmap(addr, length, prot, flags, fd, offset);
  if (ret != MAP_FAILED)
  {
    addRegionToMMaps(ret, length);
  }
  RETURN_TO_UPPER_HALF();
  return ret;
}

void *Loader::sbrkWrapper(intptr_t increment)
{
  void *addr = NULL;
  JUMP_TO_LOWER_HALF(lhInfo.lhFsAddr);
  addr = __sbrkWrapper(increment);
  RETURN_TO_UPPER_HALF();
  return addr;
}

/* Extend the process's data space by INCREMENT.
   If INCREMENT is negative, shrink data space by - INCREMENT.
   Return start of new space allocated, or -1 for errors.  */
void *Loader::__sbrkWrapper(intptr_t increment)
{
  void *oldbrk;

  DLOG(NOISE, "LH: sbrk called with 0x%lx\n", increment);

  if (__curbrk == NULL)
  {
    if (brk(0) < 0)
    {
      return (void *)-1;
    }
    else
    {
      __endOfHeap = __curbrk;
    }
  }

  if (increment == 0)
  {
    DLOG(NOISE, "LH: sbrk returning %p\n", __curbrk);
    return __curbrk;
  }

  oldbrk = __curbrk;
  if (increment > 0
          ? ((uintptr_t)oldbrk + (uintptr_t)increment < (uintptr_t)oldbrk)
          : ((uintptr_t)oldbrk < (uintptr_t)-increment))
  {
    errno = ENOMEM;
    return (void *)-1;
  }

  if ((VA)oldbrk + increment > (VA)__endOfHeap)
  {
    if (mmapWrapper(__endOfHeap,
                    ROUND_UP((VA)oldbrk + increment - (VA)__endOfHeap),
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS,
                    -1, 0) == MAP_FAILED)
    {
      return (void *)-1;
    }
  }

  __endOfHeap = (void *)ROUND_UP((VA)oldbrk + increment);
  __curbrk = (VA)oldbrk + increment;

  DLOG(NOISE, "LH: sbrk returning %p\n", oldbrk);

  return oldbrk;
}

void Loader::addRegionToMMaps(void *addr, size_t length)
{
  MmapInfo_t newRegion;
  newRegion.addr = addr;
  newRegion.len = length;
  mmaps.push_back(newRegion);
}

// Writes out the lhinfo global object to a file. Returns 0 on success,
// -1 on failure.
int Loader::writeLhInfoToFile()
{
  size_t rc = 0;
  char filename[100];
  snprintf(filename, 100, "./lhInfo_%d", getpid());
  int fd = open(filename, O_WRONLY | O_CREAT, 0644);
  if (fd < 0)
  {
    DLOG(ERROR, "Could not create addr.bin file. Error: %s", strerror(errno));
    return -1;
  }

  rc = write(fd, &lhInfo, sizeof(lhInfo));
  if (rc < sizeof(lhInfo))
  {
    DLOG(ERROR, "Wrote fewer bytes than expected to addr.bin. Error: %s", strerror(errno));
    rc = -1;
  }
  close(fd);
  return rc;
}
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
#include "loader_global_funcs.hpp"

// returns the parent's parameters start index in the command line parameters
int Loader::processCommandLineArgs(const char **argv, pair<int, int> &param_count) const
{
  vector<string> argv1, argv2;
  auto *args = &argv1;
  bool separatorFound = false;
  auto i {0};
  auto index {0};
  argv++;
  i++;
  while(*argv != nullptr)
  {
    if(strcmp(*argv, (char*)"--") == 0)
    {
      separatorFound = (!separatorFound) ? true : false;
      if(!separatorFound)
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
  if(!separatorFound || argv1.empty() || argv2.empty())
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

  // Create an AF_LOCAL socketpair used for exchanging messages
  // between the model-checker process (ourselves) and the model-checked
  // process:
  int sockets[2];
  assert((socketpair(AF_LOCAL, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) != -1) && "Could not create socketpair");

  pid_t pid = fork();
  assert(pid >= 0 && "Could not fork child process");

  if (pid == 0) // child
  {
    ::close(sockets[1]);
    channel = make_unique<Channel>(sockets[0]);
    
    ptrace(PTRACE_TRACEME, 0, nullptr, nullptr); // Parent will get notified of everything
    raise(SIGSTOP); // Wait for the parent to awake me

    runRtld(ldname, 0, param_count.first);
  }
  else // parent
  {
    ::close(sockets[0]);
    channel = make_unique<Channel>(sockets[1]);

    int status;
    auto wait_ret = waitpid(pid, &status, 0);
    
    ptrace(PTRACE_SETOPTIONS, pid, nullptr, PTRACE_O_TRACEEXIT); // I also want to know about the child's exit()
    ptrace(PTRACE_CONT, pid, 0, 0); // Let's awake the child now

    wait_ret = waitpid(pid, &status, 0);
    runRtld(ldname, param_index, param_count.second);
  }
}

// This function loads in ld.so, sets up a separate stack for it, and jumps
// to the entry point of ld.so
void Loader::runRtld(const char* ldname, int param_index, int param_count)
{
  int rc = -1;

  // Load RTLD (ld.so)  
  DynObjInfo ldso = safeLoadLib(ldname);
  std::cout << ((getpid() == _parent_pid) ? "[PARENT], " : "[CHILD], ") << "lsdo.baseAddr: " 
            << std::hex << ldso.get_base_addr() << std::endl;

  if (ldso.get_base_addr() == NULL || ldso.get_entry_point() == NULL)
  {
    DLOG(ERROR, "Error loading the runtime loader (%s). Exiting...\n", ldname);
    return;
  }

  // Create new stack region to be used by RTLD
  void *newStack = createNewStackForRtld(ldso, param_index, param_count);
  if (!newStack)
  {
    DLOG(ERROR, "Error creating new stack for RTLD. Exiting...\n");
    exit(-1);
  }

  // Create new heap region to be used by RTLD
  void *newHeap = createNewHeapForRtld();
  if (!newHeap)
  {
    DLOG(ERROR, "Error creating new heap for RTLD. Exiting...\n");
    exit(-1);
  }

  std::stringstream ss;
  ss << ((getpid() == _parent_pid) ? "[PARENT], " : "[CHILD], ") 
     << "before jumping to sp: " << std::dec << getpid();  
  pause_run(ss.str());
  print_mmapped_ranges(getpid());

  // Pointer to the ld.so entry point
  void *ldso_entrypoint = ldso.get_entry_point();

  // Change the stack pointer to point to the new stack and jump into ld.so
  asm volatile(CLEAN_FOR_64_BIT(mov %0, %%esp;)
               :
               : "g"(newStack)
               : "memory");
  asm volatile("jmp *%0"
               :
               : "g"(ldso_entrypoint)
               : "memory");

  DLOG(ERROR, "Error: RTLD returned instead of passing the control to the created stack. Panic...\n");
  exit(-1);
}

// This function allocates a new heap for (the possibly second) ld.so.
// The initial heap size is 1 page
//
// Returns the start address of the new heap on success, or NULL on
// failure.
void *Loader::createNewHeapForRtld()
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
  return addr;
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

// This function does three things:
//  1. Creates a new stack region to be used for initialization of RTLD (ld.so)
//  2. Deep copies the original stack (from the kernel) in the new stack region
//  3. Returns a pointer to the beginning of stack in the new stack region
void *Loader::createNewStackForRtld(const DynObjInfo &info, int param_index, int param_count)
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

// Creates a deep copy of the stack region pointed to be `origStack` at the
// location pointed to be `newStack`. Returns the start-of-stack pointer
// in the new stack region.
void *Loader::deepCopyStack(void *newStack, const void *origStack, size_t len,
                            const void *newStackEnd, const void *origStackEnd,
                            const DynObjInfo &info, int param_index, int param_count)
{
  // Return early if any pointer is NULL
  if (!newStack || !origStack ||
      !newStackEnd || !origStackEnd)
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

  void *origArgcAddr = (void *)get_argc_addr(origStackEnd);
  int origArgc = *(int *)origArgcAddr;
  char **origArgv = (char **)get_argv_addr(origStackEnd);
  const char **origEnv = (const char **)get_env_addr(origArgv, origArgc);

  void *newArgcAddr = (void *)get_argc_addr(newStackEnd);
  int newArgc = *(int *)newArgcAddr;
  char **newArgv = (char **)get_argv_addr(newStackEnd);
  const char **newEnv = (const char **)get_env_addr(newArgv, newArgc);
  ElfW(auxv_t) *newAuxv = get_auxv_addr(newEnv);

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
  patchAuxv(newAuxv, info.get_phnum(),
            (uintptr_t)info.get_phdr(),
            (uintptr_t)info.get_entry_point());

  // printf("newArgv[-2]: %lu \n", (unsigned long)&newArgv[0]);

  // We clear out the rest of the new stack region just in case ...
  memset(newStack, 0, (size_t)((uintptr_t)&newArgv[-2] - (uintptr_t)newStack));

  // Return the start of new stack.
  return (void *)newArgcAddr;
}

DynObjInfo Loader::safeLoadLib(const char *ld_name)
{  
  Elf64_Addr cmd_entry = getInterpreterEntry(ld_name);
  DynObjInfo info;
  auto baseAddr = load_elf_interpreter(ld_name, info);
  auto entryPoint = (void *)((unsigned long)baseAddr + (unsigned long)cmd_entry);
  info.set_base_addr(baseAddr);
  info.set_entry_point(entryPoint);
  return info;
}

void Loader::unlockReservedMemRegion()
{
  munmap(g_range->start, (unsigned long)g_range->end - (unsigned long)g_range->start);
}

void Loader::lockFreeMemRegions()
{
  std::vector<MemoryArea_t> mmaps_range {};
  Area area;
  int mapsfd = open("/proc/self/maps", O_RDONLY);
  bool firstLine = true;
  MemoryArea_t range;
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
  
  void *region = mmapWrapper(g_range->start, (unsigned long)g_range->end - (unsigned long)g_range->start, PROT_READ | PROT_WRITE,
                             MAP_GROWSDOWN | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (region == MAP_FAILED)
  {
    DLOG(ERROR, "Failed to mmap region: %s\n", strerror(errno));
  }
}

void *Loader::load_elf_interpreter(const char *elf_interpreter, DynObjInfo &info)
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

  info.set_phnum(elf_hdr.e_phnum);
  info.set_phdr((VA)baseAddr + elf_hdr.e_phoff);
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

Elf64_Addr Loader::getInterpreterEntry(const char *ld_name)
{
  int rc;
  char e_ident[EI_NIDENT];

  int fd = open(ld_name, O_RDONLY);
  assert(fd != -1);

  rc = read(fd, e_ident, sizeof(e_ident));
  assert(rc == sizeof(e_ident));
  assert(strncmp(e_ident, ELFMAG, strlen(ELFMAG)) == 0);
  assert(e_ident[EI_CLASS] == ELFCLASS64); // FIXME:  Add support for 32-bit ELF

  // Reset fd to beginning and parse file header
  lseek(fd, 0, SEEK_SET);
  Elf64_Ehdr elf_hdr;
  rc = read(fd, &elf_hdr, sizeof(elf_hdr));
  assert(rc == sizeof(elf_hdr));
  return elf_hdr.e_entry;
}

void *Loader::mmapWrapper(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
  length = ROUND_UP(length);
  void *ret = mmap(addr, length, prot, flags, fd, offset);
  return ret;
}

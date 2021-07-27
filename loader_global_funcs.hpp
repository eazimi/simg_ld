#ifndef LOADER_GLOBAL_FUNCS_HPP
#define LOADER_GLOBAL_FUNCS_HPP

#include "loader_headers.hpp"

static void *get_argc_addr(const void *stackEnd)
{
    return (void *)((uintptr_t)(stackEnd) + sizeof(uintptr_t));
}

// Returns pointer to argv[0], given a pointer to end of stack
static void *get_argv_addr(const void *stackEnd)
{
    return (void *)((unsigned long)(stackEnd) + 2 * sizeof(uintptr_t));
}

// Returns pointer to env[0], given a pointer to end of stack
static void *get_env_addr(char **argv, int argc)
{
    return (void *)&argv[argc + 1];
}

// Returns a pointer to aux vector, given a pointer to the environ vector
// on the stack
static ElfW(auxv_t) * get_auxv_addr(const char **env)
{
    ElfW(auxv_t) * auxvec;
    const char **evp = env;
    while (*evp++ != nullptr)
        ;
    auxvec = (ElfW(auxv_t) *)evp;
    return auxvec;
}

// Given a pointer to aux vector, parses the aux vector, and patches the
// following three entries: AT_PHDR, AT_ENTRY, and AT_PHNUM
static void patchAuxv(ElfW(auxv_t) * av, unsigned long phnum,
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

// Returns the /proc/self/stat entry in the out string (of length len)
static void getProcStatField(enum Procstat_t type, char *out, size_t len)
{
    const char *procPath = "/proc/self/stat";
    char sbuf[1024] = {0};

    int fd = open(procPath, O_RDONLY);
    if (fd < 0)
    {
        DLOG(ERROR, "Failed to open %s. Error: %s\n", procPath, strerror(errno));
        return;
    }

    int num_read = read(fd, sbuf, sizeof sbuf - 1);
    close(fd);
    if (num_read <= 0)
        return;
    sbuf[num_read] = '\0';

    char *field_str = strtok(sbuf, " ");
    int field_counter = 0;
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

// Returns the [stack] area by reading the proc maps
static void getStackRegion(Area *stack) // OUT
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

// Creates a deep copy of the stack region pointed to be `origStack` at the
// location pointed to be `newStack`. Returns the start-of-stack pointer
// in the new stack region.
static void *deepCopyStack(void *newStack, const void *origStack, size_t len,
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

    // in the parent process
    if (param_index == 0)
    {
        off_t argvDelta = (uintptr_t)origArgv[1] - (uintptr_t)origArgv;
        newArgv[0] = (char *)((uintptr_t)newArgv + (uintptr_t)argvDelta);
        newArgv[param_count + 1] = nullptr;
    }
    else // in the child process
    {
        newArgv[0] = newArgv[param_index];
        auto i{0};
        for (; i < param_count; i++)
            newArgv[i + 1] = newArgv[param_index + i];
        newArgv[i + 1] = nullptr;
        // *(int *)newArgcAddr = param_count + 1;
    }
    *(int *)newArgcAddr = param_count + 1;

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

    // We clear out the rest of the new stack region just in case ...
    memset(newStack, 0, (size_t)((uintptr_t)&newArgv[-2] - (uintptr_t)newStack));

    // Return the start of new stack.
    return (void *)newArgcAddr;
}

static void *mmapWrapper(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    length = ROUND_UP(length);
    void *ret = mmap(addr, length, prot, flags, fd, offset);
    return ret;
}

static void run_child_process(int socket, const function<void()> &func) 
{
  ptrace(PTRACE_TRACEME, 0, nullptr, nullptr); // Parent will get notified of everything
  int fdflags = fcntl(socket, F_GETFD, 0);
  assert((fdflags != -1 && fcntl(socket, F_SETFD, fdflags & ~FD_CLOEXEC) != -1) &&
         "Could not remove CLOEXEC for socket");
  raise(SIGSTOP); // Wait for the parent to awake me
  func();
}

// returns the parent's parameters start index in the command line parameters
static int process_argv(const char **argv, pair<int, int> &param_count)
{
  vector<string> argv1, argv2;
  auto *args = &argv1;
  bool separatorFound = false;
  auto i{0};
  auto index{0};
  argv++;
  i++;
  while (*argv != nullptr)
  {
    if (strcmp(*argv, (char *)"--") == 0)
    {
      separatorFound = (!separatorFound) ? true : false;
      if (!separatorFound)
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
  if (!separatorFound || argv1.empty() || argv2.empty())
    return -1;
  param_count.first = argv1.size();  // child process
  param_count.second = argv2.size(); // parent process
  return ++index;
}

#endif
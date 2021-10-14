#include "stack.h"
#include <fcntl.h>

Stack::Stack() {}

// Returns the [stack] area by reading the proc maps
void Stack::getStackRegion(Area *stack)
{
  Area area;
  int mapsfd = open("/proc/self/maps", O_RDONLY);
  while (readMapsLine(mapsfd, &area)) {
    if (strstr(area.name, "[stack]") && area.endAddr >= (VA)&area) {
      *stack = area;  
      break;
    }
  }
  close(mapsfd);
}

// Returns the /proc/self/stat entry in the out string (of length len)
void Stack::getProcStatField(enum Procstat_t type, char* out, size_t len)
{
  const char* procPath = "/proc/self/stat";
  char sbuf[1024]      = {0};

  int fd = open(procPath, O_RDONLY);
  if (fd < 0) {
    DLOG(ERROR, "Failed to open %s. Error: %s\n", procPath, strerror(errno));
    return;
  }

  int num_read = read(fd, sbuf, sizeof sbuf - 1);
  close(fd);
  if (num_read <= 0)
    return;
  sbuf[num_read] = '\0';

  char* field_str   = strtok(sbuf, " ");
  int field_counter = 0;
  while (field_str && field_counter != type) {
    field_str = strtok(nullptr, " ");
    field_counter++;
  }

  if (field_str) {
    strncpy(out, field_str, len);
  } else {
    DLOG(ERROR, "Failed to parse %s.\n", procPath);
  }
}

// Creates a deep copy of the stack region pointed to be `origStack` at the
// location pointed to be `newStack`. Returns the start-of-stack pointer
// in the new stack region.
void* Stack::deepCopyStack(void* newStack, const void* origStack, size_t len, const void* newStackEnd,
                           const void* origStackEnd, const DynObjInfo& info, vector<string> app_params,
                           int socket_id) const
{
  // Return early if any pointer is NULL
  if (!newStack || !origStack || !newStackEnd || !origStackEnd) {
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

  void* origArgcAddr   = (void*)getArgcAddr(origStackEnd);
  int origArgc         = *(int*)origArgcAddr;
  char** origArgv      = (char**)getArgvAddr(origStackEnd);
  const char** origEnv = (const char**)getEnvAddr(origArgv, origArgc);

  void* newArgcAddr     = (void*)getArgcAddr(newStackEnd);
  int newArgc           = *(int*)newArgcAddr;
  char** newArgv        = (char**)getArgvAddr(newStackEnd);
  const char** newEnv   = (const char**)getEnvAddr(newArgv, newArgc);
  ElfW(auxv_t)* newAuxv = getAuxvAddr(newEnv);

  // Patch the argv vector in the new stack
  //   First, set up the argv vector based on the original stack
  for (int i = 0; origArgv[i] != nullptr; i++) {
    off_t argvDelta = (uintptr_t)origArgv[i] - (uintptr_t)origArgv;
    newArgv[i]      = (char*)((uintptr_t)newArgv + (uintptr_t)argvDelta);
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

  // make up the parameters in the stack
  char* p    = newArgv[0];
  char str[] = "./mc";
  memcpy((void*)p, (void*)str, (strlen(str) + 1) * sizeof(char));
  *(p + strlen(str)) = '\0';

  auto newArgvIndex = 1;
  p += strlen(str) + 1;
  for (auto s : app_params) {
    int length            = strlen(s.c_str());
    newArgv[newArgvIndex] = p;
    memcpy((void*)p, (void*)s.c_str(), (length + 1) * sizeof(char));
    *(p + length) = '\0';
    p += length + 1;
    ++newArgvIndex;
  }

  newArgv[newArgvIndex++]    = p;
  char sock_id[16] = {'\0'};
  strcpy(sock_id, to_string(socket_id).c_str());
  int length = strlen(sock_id);
  memcpy((void*)p, (void*)sock_id, (length + 1) * sizeof(char));
  *(p + length) = '\0';

  p += length + 1;
  *p = '\0';
  newArgv[newArgvIndex] = p;

  *(int*)newArgcAddr = newArgvIndex;        
  newArgc = newArgvIndex;

  // if (param_index == 0) {

  //   char *p = newArgv[0];
  //   char str0[] = "./mc";
  //   memcpy((void*)p, (void*)str0, (strlen(str0)+1)*sizeof(char));
  //   p[strlen(str0)] = '\0';

  //   p += strlen(str0)+1;
  //   newArgv[1] = p;
  //   char str1[] = "./app";
  //   memcpy((void*)p, (void*)str1, (strlen(str1)+1)*sizeof(char));
  //   p[strlen(str1)] = '\0';

  //   p += strlen(str1)+1;
  //   newArgv[2] = p;
  //   char str2[16] = {'\0'};
  //   strcpy(str2, to_string(socket_id).c_str());
  //   memcpy((void*)p, (void*)str2, (strlen(str2)+1)*sizeof(char));
  //   p[strlen(str2)+1] = '\0';
    


  //   // sprintf((char*)newArgv[0], str0);
  //   // cout << newArgv[0] << endl;

  //   // char str1[] = "./app";
  //   // memcpy((void*)newArgv[1], (void*)str1, (strlen(str1)+1)*sizeof(char));
  //   // newArgv[1][strlen(str1)] = '\0';
  //   // cout << getpid() << ", newArgv[1]: " << newArgv[1] << endl;

  //   // char str2[16] = {'\0'};
  //   // strcpy(str2, to_string(socket_id).c_str());
  //   // memcpy((void*)newArgv[2], (void*)str2, (strlen(str2)+1)*sizeof(char));
  //   // cout << newArgv[2] << endl;

  //   // sprintf(newArgv[1], "./app");
  //   // // newArgv[0][5] = '\0';

  //   // strcpy(newArgv[2], to_string(socket_id).c_str());
  //   // newArgv[0][1] = '\0';
    
  //   newArgv[3] = nullptr;

  //   *(int*)newArgcAddr = 3;
  //   newArgc = *(int*)newArgcAddr;

  // // cout << newArgv[0] << endl;
  // // cout << newArgv[1] << ", " << strlen(newArgv[1]) << endl;
  // // cout << newArgv[2] << endl;



  // } 

  // cout << getpid() << ", origArgv: " << origArgv[origArgc] << endl;
  // cout << getpid() << ", newArgv: " << newArgv[newArgc] << endl;
  // while(true);

  // else // in the parent process
  // {
  //   newArgv[0] = newArgv[param_index];
  //   auto i{0};
  //   for (; i < param_count; i++)
  //     newArgv[i + 1] = newArgv[param_index + i];
  //   newArgv[i + 1]     = nullptr;
  //   *(int*)newArgcAddr = param_count + 1;
  // }
  // *(int *)newArgcAddr = param_count + 1;

  stringstream ss;
  // ss << getpid() << ", origArgc: " << origArgc << ", deepCopyStack(), printing parameters in orgiStack" << endl;
  // for(auto i=0; i<origArgc; i++)
  //   ss << i << ": " << origArgv[i] << endl;
  // cout << ss.str();

  // ss << getpid() << ", origArgc: " << origArgc << ", deepCopyStack(), printing parameters in orgiStack" << endl;
  // for(auto i=0; i<origArgc; i++)
  //   ss << i << ". addr: " << (unsigned long)&origArgv[i] << ", length: " << strlen(origArgv[i]) << endl;
  // cout << ss.str();

  ss.clear();
  ss << getpid() << ", newArgc: " << newArgc << ", deepCopyStack(), printing parameters in newStack" << endl;
  for(auto i=0; i<newArgc; i++)
    ss << i << ": " << newArgv[i] << endl;
  cout << ss.str();
  // cout << newArgv[0] << endl;
  // // cout << newArgv[1] << ", " << strlen(newArgv[1]) << endl;
  // cout << newArgv[1] << endl;
  // cout << newArgv[2] << endl;

  // while(true);

  // Patch the env vector in the new stack
  for (int i = 0; origEnv[i] != nullptr; i++) {
    off_t envDelta = (uintptr_t)origEnv[i] - (uintptr_t)origEnv;
    newEnv[i]      = (char*)((uintptr_t)newEnv + (uintptr_t)envDelta);
  }

  // The aux vector, which we would have inherited from the original stack,
  // has entries that correspond to the kernel loader binary. In particular,
  // it has these entries AT_PHNUM, AT_PHDR, and AT_ENTRY that correspond
  // to kernel-loader. So, we atch the aux vector in the new stack to
  // correspond to the new binary: the freshly loaded ld.so.
  patchAuxv(newAuxv, info.get_phnum(), (uintptr_t)info.get_phdr(), (uintptr_t)info.get_entry_point());

  // We clear out the rest of the new stack region just in case ...
  memset(newStack, 0, (size_t)((uintptr_t)&newArgv[-2] - (uintptr_t)newStack));
  // memset(newStack, 0, (size_t)((uintptr_t)&newArgv[-3] - (uintptr_t)newStack));

  // Return the start of new stack.
  return (void*)newArgcAddr;
}

// Creates a deep copy of the stack region pointed to be `origStack` at the
// location pointed to be `newStack`. Returns the start-of-stack pointer
// in the new stack region.
void* Stack::deepCopyStack(void* newStack, const void* origStack, size_t len, const void* newStackEnd,
                           const void* origStackEnd, const DynObjInfo& info) const
{
  // Return early if any pointer is NULL
  if (!newStack || !origStack || !newStackEnd || !origStackEnd) {
    return nullptr;
  }

  memcpy(newStack, origStack, len);

  void* origArgcAddr   = (void*)getArgcAddr(origStackEnd);
  int origArgc         = *(int*)origArgcAddr;
  char** origArgv      = (char**)getArgvAddr(origStackEnd);
  const char** origEnv = (const char**)getEnvAddr(origArgv, origArgc);

  void* newArgcAddr     = (void*)getArgcAddr(newStackEnd);
  int newArgc           = *(int*)newArgcAddr;
  char** newArgv        = (char**)getArgvAddr(newStackEnd);
  const char** newEnv   = (const char**)getEnvAddr(newArgv, newArgc);
  ElfW(auxv_t)* newAuxv = getAuxvAddr(newEnv);

  for (int i = 0; origArgv[i] != nullptr; i++) {
    off_t argvDelta = (uintptr_t)origArgv[i] - (uintptr_t)origArgv;
    newArgv[i]      = (char*)((uintptr_t)newArgv + (uintptr_t)argvDelta);
  }
  // strcpy(newArgv[0], app);

  // cout << "main.cpp -> deepCopyStack(), printing parameters in orgiStack" << endl;
  // for(auto i=0; newArgv[i] != nullptr; i++)
  //   cout << i << ": " << origArgv[i] << endl;

  // cout << "main.cpp -> deepCopyStack(), printing parameters in newStack" << endl;
  // for(auto i=0; newArgv[i] != nullptr; i++)
  //   cout << i << ": " << newArgv[i] << endl;

  // Patch the env vector in the new stack
  for (int i = 0; origEnv[i] != nullptr; i++) {
    off_t envDelta = (uintptr_t)origEnv[i] - (uintptr_t)origEnv;
    newEnv[i]      = (char*)((uintptr_t)newEnv + (uintptr_t)envDelta);
  }

  patchAuxv(newAuxv, info.get_phnum(), (uintptr_t)info.get_phdr(), (uintptr_t)info.get_entry_point());

  // We clear out the rest of the new stack region just in case ...
  memset(newStack, 0, (size_t)((uintptr_t)&newArgv[-2] - (uintptr_t)newStack));

  // Return the start of new stack.
  return (void*)newArgcAddr;
}

void* Stack::getArgcAddr(const void* stackEnd) const
{
  return (void*)((uintptr_t)(stackEnd) + sizeof(uintptr_t));
}

// Returns pointer to argv[0], given a pointer to end of stack
void* Stack::getArgvAddr(const void* stackEnd) const
{
  return (void*)((unsigned long)(stackEnd) + 2 * sizeof(uintptr_t));
}

// Returns pointer to env[0], given a pointer to end of stack
void* Stack::getEnvAddr(char** argv, int argc) const
{
  return (void*)&argv[argc + 1];
}

// Returns a pointer to aux vector, given a pointer to the environ vector on the stack
ElfW(auxv_t) * Stack::getAuxvAddr(const char** env) const
{
  ElfW(auxv_t) * auxvec;
  const char** evp = env;
  while (*evp++ != nullptr)
    ;
  auxvec = (ElfW(auxv_t)*)evp;
  return auxvec;
}

/* 
  Given a pointer to aux vector, parses the aux vector, and patches the
  following three entries: AT_PHDR, AT_ENTRY, and AT_PHNUM 
*/
void Stack::patchAuxv(ElfW(auxv_t) * av, unsigned long phnum, unsigned long phdr, unsigned long entry) const
{
  for (; av->a_type != AT_NULL; ++av) {
    switch (av->a_type) {
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

// This function does three things:
//  1. Creates a new stack region to be used for initialization of RTLD (ld.so)
//  2. Deep copies the original stack (from the kernel) in the new stack region
//  3. Returns a pointer to the beginning of stack in the new stack region
void* Stack::createNewStack(const DynObjInfo& info, void* stackStartAddr, vector<string> app_params, int socket_id)
{
  Area stack;
  char stackEndStr[20] = {0};
  getStackRegion(&stack);

  // 1. Allocate new stack region
  // We go through the mmap wrapper function to ensure that this gets added
  // to the list of upper half regions to be checkpointed.
  void* newStack =
      mmapWrapper(stackStartAddr, stack.size, PROT_READ | PROT_WRITE, MAP_GROWSDOWN | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (newStack == MAP_FAILED) {
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
  unsigned long origStackEnd    = atol(stackEndStr) - sizeof(unsigned long);
  unsigned long origStackOffset = origStackEnd - (unsigned long)stack.addr;
  unsigned long newStackOffset  = origStackOffset;
  void* newStackEnd             = (void*)((unsigned long)newStack + newStackOffset);

  // printf("origStack: %lu origStackOffset: %lu OrigStackEnd: %lu \n", (unsigned long)stack.addr, (unsigned
  // long)origStackOffset, (unsigned long)origStackEnd); printf("newStack: %lu newStackOffset: %lu newStackEnd: %lu \n",
  // (unsigned long)newStack, (unsigned long)newStackOffset, (unsigned long)newStackEnd);

  // 2. Deep copy stack
  newStackEnd = deepCopyStack(newStack, stack.addr, stack.size, (void*)newStackEnd, (void*)origStackEnd, info,
                              app_params, socket_id);

  return newStackEnd;
}

void* Stack::createNewStack(const DynObjInfo& info, void* stackStartAddr)
{
  Area stack;
  char stackEndStr[20] = {0};
  getStackRegion(&stack);
  void* newStack =
      mmapWrapper(stackStartAddr, stack.size, PROT_READ | PROT_WRITE, MAP_GROWSDOWN | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (newStack == MAP_FAILED) {
    DLOG(ERROR, "Failed to mmap new stack region: %s\n", strerror(errno));
    return nullptr;
  }
  getProcStatField(STARTSTACK, stackEndStr, sizeof stackEndStr);
  unsigned long origStackEnd    = atol(stackEndStr) - sizeof(unsigned long);
  unsigned long origStackOffset = origStackEnd - (unsigned long)stack.addr;
  unsigned long newStackOffset  = origStackOffset;
  void* newStackEnd             = (void*)((unsigned long)newStack + newStackOffset);
  newStackEnd = deepCopyStack(newStack, stack.addr, stack.size, (void*)newStackEnd, (void*)origStackEnd, info);

  return newStackEnd;
}

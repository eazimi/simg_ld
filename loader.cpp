#include "loader.h"

int Loader::init(const char** argv, pair<int, int>& param_count)
{
  auto param_index = process_argv(argv, param_count);
  if (param_index == -1) {
    DLOG(ERROR, "Command line parameters are invalid\n");
    DLOG(ERROR, "Usage: ./simg_ld /PATH/TO/APP1 [APP1_PARAMS] -- /PATH/TO/APP2 [APP2_PARAMS]\n");
    DLOG(ERROR, "exiting ...\n");
    exit(-1);
  }

  _parent_pid = getpid();

  // reserve some 2 GB in the address space, lock remained free areas
  reserve_memory_region();
  hide_free_memory_regions();
  release_reserved_memory_region();

  return param_index;
}

void Loader::run(const char** argv)
{
  std::pair<int, int> param_count;
  auto param_index = init((const char**)argv, param_count);

  std::stringstream ss;
  ss << "[PARENT], before fork: getpid() = " << std::dec << getpid();
  pause_run(ss.str());

  char* ldname = (char*)LD_NAME;

  // 0: ldname, 1: param_index, 2: pair->param_count
  args_ = make_tuple((char*)LD_NAME, param_index, param_count);

  // Create an AF_LOCAL socketpair used for exchanging messages
  // between the model-checker process (ourselves) and the model-checked
  // process:
  int sockets[2];
  assert((socketpair(AF_LOCAL, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) != -1) && "Could not create socketpair");

  pid_t pid = fork();
  assert(pid >= 0 && "Could not fork child process");
  procs_.insert(pid);

  if (pid == 0) // child
  {
    ::close(sockets[1]);
    run_child_process(sockets[0], [&]() { run_rtld(ldname, 0, param_count.first, sockets[0]); });
  } else // parent
  {
    ::close(sockets[0]);
    sync_proc_ = make_unique<SyncProc>(sockets[1]);
    sync_proc_->start(
        [](evutil_socket_t sig, short event, void* arg) {
          auto loader = static_cast<Loader*>(arg);
          if (event == EV_READ) {
            // DLOG(NOISE, "parent: EV_READ received\n");
            std::array<char, MESSAGE_LENGTH> buffer;
            ssize_t size = loader->sync_proc_->get_channel().receive(buffer.data(), buffer.size(), false);
            if (size == -1 && errno != EAGAIN) {
              DLOG(ERROR, "%s\n", strerror(errno));
              exit(-1);
            }

            vector<string> str_messages{"NONE", "READY", "CONTINUE", "FINISH", "DONE"};
            s_message_t base_message;
            memcpy(&base_message, buffer.data(), sizeof(base_message));
            auto str_message_type = str_messages[static_cast<int>(base_message.type)];
            DLOG(INFO, "parent: child sent a %s message\n", str_message_type.c_str());

            base_message.pid = getpid();
            bool run_app     = false;
            if (base_message.type == MessageType::READY) {
              base_message.type = MessageType::CONTINUE;
              // DLOG(INFO, "parent: sending a %s message to the child ...\n", "CONTINUE");
            } else if (base_message.type == MessageType::FINISH) {
              base_message.type = MessageType::DONE;
              run_app           = true;
              // DLOG(INFO, "parent: sending a %s message to the child ...\n", "DONE");
            }
            loader->sync_proc_->get_channel().send(base_message);
            if (run_app) { // 0: ldname, 1: param_index, 2: param_count
              loader->run_rtld(get<0>(loader->args_), get<1>(loader->args_), get<2>(loader->args_).second);
            } 
            // if (!sync_proc->handle_message(buffer.data(), size))
            //   sync_proc->break_loop();
          } else if (event == EV_SIGNAL) {
            if (sig == SIGCHLD) {
              // DLOG(NOISE, "parent: EV_SIGNAL received\n");
              loader->handle_waitpid();
            }
          } else {
            DLOG(ERROR, "Unexpected event\n");
            exit(-1);
          }
        },
        this);
  }
}

void Loader::remove_process(pid_t pid)
{
  auto it = procs_.find(pid);
  if (it != procs_.end())
    procs_.erase(it);
}

void Loader::handle_waitpid()
{
  int status;
  pid_t pid;
  while ((pid = waitpid(-1, &status, WNOHANG)) != 0) {
    if (pid == -1) {
      if (errno == ECHILD) {
        // No more children:
        assert((procs_.size() == 0) && "Inconsistent state");
        break;
      } else {
        DLOG(ERROR, "Could not wait for pid\n");
        exit(-1);
      }
    }

    unordered_set<pid_t>::iterator it = procs_.find(pid);
    if (it == procs_.end()) {
      DLOG(ERROR, "Child process not found\n");
      return;
    } else {
      // From PTRACE_O_TRACEEXIT:
#ifdef __linux__
      if (status >> 8 == (SIGTRAP | (PTRACE_EVENT_EXIT << 8))) {
        assert((ptrace(PTRACE_GETEVENTMSG, pid, 0, &status) != -1) && "Could not get exit status");
        if (WIFSIGNALED(status)) {
          DLOG(ERROR, "CRASH IN THE PROGRAM, %i\n", status);
          for (auto process : procs_)
            kill(process, SIGKILL);
          exit(-1);
        }
      }
#endif

      // We don't care about signals, just reinject them:
      if (WIFSTOPPED(status)) {
        // DLOG(INFO, "Stopped with signal %i\n", (int)WSTOPSIG(status));
        errno = 0;
#ifdef __linux__
        ptrace(PTRACE_CONT, pid, 0, WSTOPSIG(status));
#endif
        assert(errno == 0 && "Could not PTRACE_CONT");
      } else if (WIFSIGNALED(status)) {
        DLOG(ERROR, "CRASH IN THE PROGRAM, %i\n", status);
        for (auto process : procs_)
          kill(process, SIGKILL);
        exit(-1);
      } else if (WIFEXITED(status)) {
        DLOG(INFO, "Child process is over\n");
        procs_.erase(it);
      }
    }
  }
}

// This function loads in ld.so, sets up a separate stack for it, and jumps
// to the entry point of ld.so
void Loader::run_rtld(const char* ldname, int param_index, int param_count, int socket_id)
{
  int rc = -1;

  // Load RTLD (ld.so)
  DynObjInfo ldso = load_lsdo(ldname);

  if (ldso.get_base_addr() == NULL || ldso.get_entry_point() == NULL) {
    DLOG(ERROR, "Error loading the runtime loader (%s). Exiting...\n", ldname);
    return;
  }

  // Create new stack region to be used by RTLD
  void* newStack = create_new_stack_for_ldso(ldso, param_index, param_count, socket_id);
  if (!newStack) {
    DLOG(ERROR, "Error creating new stack for RTLD. Exiting...\n");
    exit(-1);
  }

  // Create new heap region to be used by RTLD
  void* newHeap = create_new_heap_for_ldso();
  if (!newHeap) {
    DLOG(ERROR, "Error creating new heap for RTLD. Exiting...\n");
    exit(-1);
  }

  std::stringstream ss;
  ss << ((getpid() == _parent_pid) ? "[PARENT], " : "[CHILD], ") << "before jumping to sp: " << std::dec << getpid();
  pause_run(ss.str());
  // print_mmapped_ranges(getpid());

  // Pointer to the ld.so entry point
  void* ldso_entrypoint = ldso.get_entry_point();

  // Change the stack pointer to point to the new stack and jump into ld.so
  asm volatile(CLEAN_FOR_64_BIT(mov %0, %%esp;) : : "g"(newStack) : "memory");
  asm volatile("jmp *%0" : : "g"(ldso_entrypoint) : "memory");

  DLOG(ERROR, "Error: RTLD returned instead of passing the control to the created stack. Panic...\n");
  exit(-1);
}

// This function allocates a new heap for (the possibly second) ld.so.
// The initial heap size is 1 page
//
// Returns the start address of the new heap on success, or NULL on
// failure.
void* Loader::create_new_heap_for_ldso()
{
  const uint64_t heapSize = 100 * PAGE_SIZE;

  // We go through the mmap wrapper function to ensure that this gets added
  // to the list of upper half regions to be checkpointed.

  void* startAddr = (void*)((unsigned long)g_range_->start + _1500_MB);

  void* addr = mmapWrapper(startAddr /*0*/, heapSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  if (addr == MAP_FAILED) {
    DLOG(ERROR, "Failed to mmap region. Error: %s\n", strerror(errno));
    return NULL;
  }
  // Add a guard page before the start of heap; this protects
  // the heap from getting merged with a "previous" region.
  mprotect(addr, PAGE_SIZE, PROT_NONE);
  return addr;
}

// This function does three things:
//  1. Creates a new stack region to be used for initialization of RTLD (ld.so)
//  2. Deep copies the original stack (from the kernel) in the new stack region
//  3. Returns a pointer to the beginning of stack in the new stack region
void* Loader::create_new_stack_for_ldso(const DynObjInfo& info, int param_index, int param_count, int socket_id)
{
  Area stack;
  char stackEndStr[20] = {0};
  getStackRegion(&stack);

  // 1. Allocate new stack region
  // We go through the mmap wrapper function to ensure that this gets added
  // to the list of upper half regions to be checkpointed.

  void* startAddr = (void*)((unsigned long)g_range_->start + _1_GB);

  void* newStack =
      mmapWrapper(startAddr, stack.size, PROT_READ | PROT_WRITE, MAP_GROWSDOWN | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
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
                              param_index, param_count, socket_id);

  return newStackEnd;
}

DynObjInfo Loader::load_lsdo(const char* ld_name)
{
  Elf64_Addr cmd_entry = get_interpreter_entry(ld_name);
  DynObjInfo info;
  auto baseAddr   = load_elf_interpreter(ld_name, info);
  auto entryPoint = (void*)((unsigned long)baseAddr + (unsigned long)cmd_entry);
  info.set_base_addr(baseAddr);
  info.set_entry_point(entryPoint);
  return info;
}

void Loader::release_reserved_memory_region()
{
  munmap(g_range_->start, (unsigned long)g_range_->end - (unsigned long)g_range_->start);
}

void Loader::hide_free_memory_regions()
{
  std::vector<MemoryArea_t> mmaps_range{};
  Area area;
  int mapsfd     = open("/proc/self/maps", O_RDONLY);
  bool firstLine = true;
  MemoryArea_t range;
  while (readMapsLine(mapsfd, &area)) {
    // todo: check if required to add this condition: (area.endAddr >= (VA)&area)
    if (firstLine) {
      range.start = area.endAddr;
      firstLine   = false;
      continue;
    }
    range.end = area.addr;
    mmaps_range.push_back(std::move(range));
    range.start = area.endAddr;
  }
  close(mapsfd);
  mmaps_range.pop_back();

  auto mmaps_size = mmaps_range.size() - 1;
  for (auto i = 0; i <= mmaps_size; i++) {
    auto start_mmap = (unsigned long)(mmaps_range[i].start);
    auto length     = (unsigned long)(mmaps_range[i].end) - start_mmap;
    if (length == 0)
      continue;
    void* mmap_ret = mmap((void*)start_mmap, length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (mmap_ret == MAP_FAILED) {
      DLOG(ERROR, "failed to lock the free spot. %s\n", strerror(errno));
      exit(-1);
    }
  }
}

void Loader::reserve_memory_region()
{
  Area area;
  bool found = false;
  int mapsfd = open("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    DLOG(ERROR, "Failed to open proc maps\n");
    return;
  }
  while (readMapsLine(mapsfd, &area)) {
    if (strstr(area.name, "[stack]")) {
      found = true;
      break;
    }
  }
  close(mapsfd);

  if (found) {
    g_range_->start = (VA)area.addr - _3_GB;
    g_range_->end   = (VA)area.addr - _1_GB;
  }
  // std::cout << "setReservedMemRange(): start = " << std::hex << g_range->start << " , end = " << g_range->end <<
  // std::endl;

  void* region = mmapWrapper(g_range_->start, (unsigned long)g_range_->end - (unsigned long)g_range_->start,
                             PROT_READ | PROT_WRITE, MAP_GROWSDOWN | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (region == MAP_FAILED) {
    DLOG(ERROR, "Failed to mmap region: %s\n", strerror(errno));
  }
}

void* Loader::load_elf_interpreter(const char* elf_interpreter, DynObjInfo& info)
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

  Elf64_Phdr* phdr;
  Elf64_Ehdr* ehdr = &elf_hdr;
  ssize_t sz       = ehdr->e_phnum * sizeof(Elf64_Phdr);
  phdr             = (Elf64_Phdr*)alloca(sz);

  if (read(ld_so_fd, phdr, sz) != sz)
    DLOG(ERROR, "can't read program header");

  unsigned long baseAddr = map_elf_interpreter_load_segment(ld_so_fd, ehdr, phdr);

  info.set_phnum(elf_hdr.e_phnum);
  info.set_phdr((VA)baseAddr + elf_hdr.e_phoff);
  return (void*)baseAddr;
}

unsigned long Loader::map_elf_interpreter_load_segment(int fd, Elf64_Ehdr* ehdr, Elf64_Phdr* phdr)
{
  unsigned long minva, maxva;
  Elf64_Phdr* iter;
  ssize_t sz;
  int flags, dyn = ehdr->e_type == ET_DYN;
  unsigned char *p, *base;

  minva = (unsigned long)-1;
  maxva = 0;

  for (iter = phdr; iter < &phdr[ehdr->e_phnum]; iter++) {
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
  base = (unsigned char*)mmap(g_range_->start, maxva - minva, PROT_NONE, flags, -1, 0);
  if (base == (void*)-1)
    return -1;
  munmap(base, maxva - minva);

  // base = (unsigned char*) g_range->start;

  flags = MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE;
  // flags = MAP_FIXED | MAP_PRIVATE;
  /* Now map each segment separately in precalculated address. */
  for (iter = phdr; iter < &phdr[ehdr->e_phnum]; iter++) {
    unsigned long off, start;
    if (iter->p_type != PT_LOAD)
      continue;
    off   = iter->p_vaddr & ALIGN;
    start = dyn ? (unsigned long)base : 0;
    // start = (unsigned long)g_range->start;
    start += TRUNC_PG(iter->p_vaddr);
    sz = ROUND_PG(iter->p_memsz + off);

    p = (unsigned char*)mmap((void*)start, sz, PROT_WRITE, flags, -1, 0);
    if (p == (void*)-1) {
      munmap(base, maxva - minva);
      return LOAD_ERR;
    }
    if (lseek(fd, iter->p_offset, SEEK_SET) < 0) {
      munmap(base, maxva - minva);
      return LOAD_ERR;
    }
    if (read(fd, p + off, iter->p_filesz) != (ssize_t)iter->p_filesz) {
      munmap(base, maxva - minva);
      return LOAD_ERR;
    }
    mprotect(p, sz, PFLAGS(iter->p_flags));
  }

  return (unsigned long)base;
}

Elf64_Addr Loader::get_interpreter_entry(const char* ld_name)
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
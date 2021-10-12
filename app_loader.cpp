#include "app_loader.h"
#include "global.hpp"
#include <assert.h>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

AppLoader::AppLoader()
{
  userSpace_ = make_unique<UserSpace>();
}

Elf64_Addr AppLoader::get_interpreter_entry(const char* ld_name)
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

unsigned long AppLoader::loadSegment(void* startAddr, int fd, Elf64_Ehdr* ehdr, Elf64_Phdr* phdr)
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
  base = (unsigned char*)mmap(startAddr, maxva - minva, PROT_NONE, flags, -1, 0);
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

void* AppLoader::loadInterpreter(void* startAddr, const char* elf_interpreter, DynObjInfo& info)
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

  unsigned long baseAddr = loadSegment(startAddr, ld_so_fd, ehdr, phdr);

  info.set_phnum(elf_hdr.e_phnum);
  info.set_phdr((VA)baseAddr + elf_hdr.e_phoff);
  return (void*)baseAddr;
}

DynObjInfo AppLoader::load_lsdo(void* startAddr, const char* ld_name)
{
  Elf64_Addr cmd_entry = get_interpreter_entry(ld_name);
  DynObjInfo info;
  auto baseAddr   = loadInterpreter(startAddr, ld_name, info);
  auto entryPoint = (void*)((unsigned long)baseAddr + (unsigned long)cmd_entry);
  info.set_base_addr(baseAddr);
  info.set_entry_point(entryPoint);
  return info;
}

// This function loads in ld.so, sets up a separate stack for it, and jumps
// to the entry point of ld.so
void AppLoader::runRtld(int param_index, int param_count, int socket_id)
{
  int rc = -1;
  auto startAddr = userSpace_->getStartAddr();
  // Load RTLD (ld.so)
  DynObjInfo ldso = load_lsdo(startAddr, (char*)LD_NAME);

  if (ldso.get_base_addr() == NULL || ldso.get_entry_point() == NULL) {
    DLOG(ERROR, "Error loading the runtime loader (%s). Exiting...\n", (char*)LD_NAME);
    return;
  }
  cout << "ldso addr: " << std::hex << startAddr << endl;

  // Create new stack region to be used by RTLD
  auto stackStartAddr = (void*)((unsigned long)startAddr + GB1);
  void* newStack = userSpace_->createNewStack(ldso, stackStartAddr, param_index, param_count, socket_id);
  cout << "stack addr: " << std::hex << stackStartAddr << endl;
  if (!newStack) {
    DLOG(ERROR, "Error creating new stack for RTLD. Exiting...\n");
    exit(-1);
  }

  // Create new heap region to be used by RTLD
  void* heapStartAddr = (void*)((unsigned long)startAddr + GB2);
  cout << "heap addr: " << std::hex << heapStartAddr << endl;
  void* newHeap = userSpace_->createNewHeap(heapStartAddr);
  if (!newHeap) {
    DLOG(ERROR, "Error creating new heap for RTLD. Exiting...\n");
    exit(-1);
  }

  std::stringstream ss;
  ss << ((getpid() == _parent_pid) ? "[PARENT], " : "[CHILD], ") << "before jumping to sp: " << std::dec << getpid();
  pause_run(ss.str());
  // print_mmapped_ranges(getpid());
  write_mmapped_ranges("before_jump_run_rtld", getpid());

  // Pointer to the ld.so entry point
  void* ldso_entrypoint = ldso.get_entry_point();

  // Change the stack pointer to point to the new stack and jump into ld.so
  asm volatile(CLEAN_FOR_64_BIT(mov %0, %%esp;) : : "g"(newStack) : "memory");
  asm volatile("jmp *%0" : : "g"(ldso_entrypoint) : "memory");

  DLOG(ERROR, "Error: RTLD returned instead of passing the control to the created stack. Panic...\n");
  exit(-1);
}

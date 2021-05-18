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
#include "switch_context.h"

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

typedef char *VA; /* VA = virtual address */

#define PAGE_SIZE 0x1000LL

// FIXME: 0x1000 is one page; Use sysconf(PAGESIZE) instead.
#define ROUND_DOWN(x) ((unsigned long long)(x) & ~(unsigned long long)(PAGE_SIZE - 1))
#define ROUND_UP(x) (((unsigned long long)(x) + PAGE_SIZE - 1) & \
                     ~(PAGE_SIZE - 1))
#define PAGE_OFFSET(x) ((x) & (PAGE_SIZE - 1))

#define MMAP_OFF_HIGH_MASK ((-(4096ULL << 1) << (8 * sizeof(off_t) - 1)))
#define MMAP_OFF_LOW_MASK (4096ULL - 1)
#define MMAP_OFF_MASK (MMAP_OFF_HIGH_MASK | MMAP_OFF_LOW_MASK)

// This function loads in ld.so, sets up a separate stack for it, and jumps
// to the entry point of ld.so
void Loader::runRtld(int argc, char **argv)
{
  int rc = -1;

  // Pointer to the ld.so entry point
  void *ldso_entrypoint = nullptr;

  // Load RTLD (ld.so)
  if (argc < 2)
  {
    DLOG(ERROR, "Usage: ./simg_ld /path/to/program [application arguments ...]\n");
    return;
  }

  // setup lower-half info including cuda APIs function pointers
  rc = setupLowerHalfInfo();
  if (rc < 0)
  {
    DLOG(ERROR, "Failed to set up lhinfo for the upper half. Exiting...\n");
    exit(-1);
  }

  void *ld_so_addr = nullptr;
  int ld_so_fd;
  Elf64_Addr cmd_entry, ld_so_entry;
  char elf_interpreter[MAX_ELF_INTERP_SZ];

  get_elf_interpreter(argv[1], &cmd_entry, elf_interpreter, ld_so_addr);

  if (!elf_interpreter)
  {
    DLOG(ERROR, "Could not find interpreter\n");
    return;
  }

  DynObjInfo_t ldso = safeLoadLib(elf_interpreter);
  // if (ldso.baseAddr == nullptr || ldso.entryPoint == nullptr)
  // {
  //   DLOG(ERROR, "Error loading the runtime loader (%s). Exiting...\n", elf_interpreter);
  //   return;
  // }

  DLOG(INFO, "New ld.so loaded at: %p\n", ldso.baseAddr);
  // ldso_entrypoint = getEntryPoint(ldso);

  // // Create new stack region to be used by RTLD
  // void *newStack = createNewStackForRtld(&ldso);
  // if (!newStack)
  // {
  //     DLOG(ERROR, "Error creating new stack for RTLD. Exiting...\n");
  //     exit(-1);
  // }
  // DLOG(INFO, "New stack start at: %p\n", newStack);

  // // Create new heap region to be used by RTLD
  // void *newHeap = createNewHeapForRtld(&ldso);
  // if (!newHeap)
  // {
  //     DLOG(ERROR, "Error creating new heap for RTLD. Exiting...\n");
  //     exit(-1);
  // }
  // DLOG(INFO, "New heap mapped at: %p\n", newHeap);

  // // insert a trampoline from ldso mmap address to mmapWrapper
  // rc = insertTrampoline(ldso.mmapAddr, (void *)&mmapWrapper);
  // if (rc < 0)
  // {
  //     DLOG(ERROR, "Error inserting trampoline for mmap. Exiting...\n");
  //     exit(-1);
  // }
  // // insert a trampoline from ldso sbrk address to sbrkWrapper
  // rc = insertTrampoline(ldso.sbrkAddr, (void *)&sbrkWrapper);
  // if (rc < 0)
  // {
  //     DLOG(ERROR, "Error inserting trampoline for sbrk. Exiting...\n");
  //     exit(-1);
  // }

  // // Everything is ready, let's set up the lower-half info struct for the upper
  // // half to read from
  // rc = setupLowerHalfInfo();
  // if (rc < 0)
  // {
  //     DLOG(ERROR, "Failed to set up lhinfo for the upper half. Exiting...\n");
  //     exit(-1);
  // }

  // // Change the stack pointer to point to the new stack and jump into ld.so
  // // TODO: Clean up all the registers?
  // asm volatile(CLEAN_FOR_64_BIT(mov % 0, % % esp;)
  //              :
  //              : "g"(newStack)
  //              : "memory");
  // asm volatile("jmp *%0"
  //              :
  //              : "g"(ldso_entrypoint)
  //              : "memory");
}

DynObjInfo_t Loader::safeLoadLib(const char *name)
{
  void *ld_so_addr = nullptr;
  DynObjInfo_t info = {0};

  int ld_so_fd;
  Elf64_Addr cmd_entry, ld_so_entry;
  ld_so_fd = open(name, O_RDONLY);
  assert(ld_so_fd != -1);
  info.baseAddr = load_elf_interpreter(ld_so_fd, name, &ld_so_entry, ld_so_addr, &info);
  off_t mmap_offset;
  off_t sbrk_offset;
  // #if UBUNTU
  //   char buf[256] = "/usr/lib/debug";
  //   buf[sizeof(buf) - 1] = '\0';
  //   ssize_t rc = 0;
  //   rc = readlink(elf_interpreter, buf + strlen(buf), sizeof(buf) - strlen(buf) - 1);
  //   if (rc != -1 && access(buf, F_OK) == 0)
  //   {
  //     // Debian family (Ubuntu, etc.) use this scheme to store debug symbols.
  //     //   http://sourceware.org/gdb/onlinedocs/gdb/Separate-Debug-Files.html
  //     fprintf(stderr, "Debug symbols for interpreter in: %s\n", buf);
  //   }
  //   printf("%s\n", buf);
  //   int debug_ld_so_fd = open(buf, O_RDONLY);
  //   assert(debug_ld_so_fd != -1);
  //   mmap_offset = get_symbol_offset(debug_ld_so_fd, buf, "mmap");
  //   sbrk_offset = get_symbol_offset(debug_ld_so_fd, buf, "sbrk");
  //   close(debug_ld_so_fd);
  // #else
  //   mmap_offset = get_symbol_offset(ld_so_fd, name, "mmap");
  //   sbrk_offset = get_symbol_offset(ld_so_fd, name, "sbrk");
  // #endif
  //   assert(mmap_offset);
  //   assert(sbrk_offset);
  //   info.mmapAddr = (VA)info.baseAddr + mmap_offset;
  //   info.sbrkAddr = (VA)info.baseAddr + sbrk_offset;
  //   // FIXME: The ELF Format manual says that we could pass the ld_so_fd to ld.so,
  //   //   and it would use that to load it.
  //   close(ld_so_fd);
  //   info.entryPoint = (void *)((unsigned long)info.baseAddr +
  //                              (unsigned long)cmd_entry);
  return info;
}

void *Loader::load_elf_interpreter(int fd, const char *elf_interpreter,
                                   Elf64_Addr *ld_so_entry, void *ld_so_addr,
                                   DynObjInfo_t *info)
{
  char e_ident[EI_NIDENT];
  int rc;
  int firstTime = 1;
  void *baseAddr = nullptr;

  rc = read(fd, e_ident, sizeof(e_ident));
  assert(rc == sizeof(e_ident));
  assert(strncmp(e_ident, ELFMAG, sizeof(ELFMAG) - 1) == 0);
  // FIXME:  Add support for 32-bit ELF later
  assert(e_ident[EI_CLASS] == ELFCLASS64);

  // Reset fd to beginning and parse file header
  lseek(fd, 0, SEEK_SET);
  Elf64_Ehdr elf_hdr;
  rc = read(fd, &elf_hdr, sizeof(elf_hdr));
  assert(rc == sizeof(elf_hdr));

  // Find ELF interpreter
  int phoff = elf_hdr.e_phoff;
  Elf64_Phdr phdr;
  int i;
  lseek(fd, phoff, SEEK_SET);
  for (i = 0; i < elf_hdr.e_phnum; i++)
  {
    rc = read(fd, &phdr, sizeof(phdr)); // Read consecutive program headers
    assert(rc == sizeof(phdr));
    if (phdr.p_type == PT_LOAD)
    {
      // PT_LOAD is the only type of loadable segment for ld.so
      auto map_addr = map_elf_interpreter_load_segment(fd, phdr, ld_so_addr);
      if (firstTime)
      {
        baseAddr = map_addr;
        firstTime = 0;
      }
    }
  }
  info->phnum = elf_hdr.e_phnum;
  info->phdr = (VA)baseAddr + elf_hdr.e_phoff;
  return baseAddr;
}

void *Loader::map_elf_interpreter_load_segment(int fd, Elf64_Phdr phdr, void *ld_so_addr)
{
  static char *base_address = nullptr; // is NULL on call to first LOAD segment
  static int first_time = 1;
  int prot = PROT_NONE;
  if (phdr.p_flags & PF_R)
    prot |= PROT_READ;
  if (phdr.p_flags & PF_W)
    prot |= PROT_WRITE;
  if (phdr.p_flags & PF_X)
    prot |= PROT_EXEC;
  assert(phdr.p_memsz >= phdr.p_filesz);
  // NOTE:  man mmap says:
  // For a file that is not a  multiple  of  the  page  size,  the
  // remaining memory is zeroed when mapped, and writes to that region
  // are not written out to the file.
  void *rc2;
  // Check ELF Format constraint:
  if (phdr.p_align > 1)
  {
    assert(phdr.p_vaddr % phdr.p_align == phdr.p_offset % phdr.p_align);
  }
  int vaddr = phdr.p_vaddr;

  int flags = MAP_PRIVATE;
  unsigned long addr = ROUND_DOWN(base_address + vaddr);
  size_t size = ROUND_UP(phdr.p_filesz + PAGE_OFFSET(phdr.p_vaddr));
  off_t offset = phdr.p_offset - PAGE_OFFSET(phdr.p_vaddr);

  // phdr.p_vaddr = ROUND_DOWN(phdr.p_vaddr);
  // phdr.p_offset = ROUND_DOWN(phdr.p_offset);
  // phdr.p_memsz = phdr.p_memsz + (vaddr - phdr.p_vaddr);
  // NOTE:  base_address is 0 for first load segment
  if (first_time)
  {
    printf("size %d \n", (int)phdr.p_filesz);
    phdr.p_vaddr += (unsigned long long)ld_so_addr;
    size = 0x27000;
  }
  else
  {
    flags |= MAP_FIXED;
  }
  if (ld_so_addr)
  {
    flags |= MAP_FIXED;
  }
  // FIXME:  On first load segment, we should map 0x400000 (2*phdr.p_align),
  //         and then unmap the unused portions later after all the
  //         LOAD segments are mapped.  This is what ld.so would do.
  rc2 = mmapWrapper((void *)addr, size, prot, flags, fd, offset);
  if (rc2 == MAP_FAILED)
  {
    DLOG(ERROR, "Failed to map memory region at %p. Error:%s\n", (void *)addr, strerror(errno));
    return nullptr;
  }
  unsigned long startBss = (uintptr_t)base_address +
                           phdr.p_vaddr + phdr.p_filesz;
  unsigned long endBss = (uintptr_t)base_address + phdr.p_vaddr + phdr.p_memsz;
  // Required by ELF Format:
  if (phdr.p_memsz > phdr.p_filesz)
  {
    // This condition is true for the RW (data) segment of ld.so
    // We need to clear out the rest of memory contents, similarly to
    // what the kernel would do. See here:
    //   https://elixir.bootlin.com/linux/v4.18.11/source/fs/binfmt_elf.c#L905
    // Note that p_memsz indicates end of data (&_end)

    // First, get to the page boundary
    uintptr_t endByte = ROUND_UP(startBss);
    // Next, figure out the number of bytes we need to clear out.
    // From Bss to the end of page.
    size_t bytes = endByte - startBss;
    memset((void *)startBss, 0, bytes);
  }
  // If there's more bss that overflows to another page, map it in and
  // zero it out
  startBss = ROUND_UP(startBss);
  endBss = ROUND_UP(endBss);
  if (endBss > startBss)
  {
    void *base = (void *)startBss;
    size_t len = endBss - startBss;
    flags |= MAP_ANONYMOUS; // This should give us 0-ed out pages
    rc2 = mmapWrapper(base, len, prot, flags, -1, 0);
    if (rc2 == MAP_FAILED)
    {
      DLOG(ERROR, "Failed to map memory region at %p. Error:%s\n", (void *)startBss, strerror(errno));
      return nullptr;
    }
  }
  if (first_time)
  {
    first_time = 0;
    base_address = (char *)rc2;
  }
  return base_address;
}

void Loader::get_elf_interpreter(char *name, Elf64_Addr *cmd_entry, char *elf_interpreter, void *ld_so_addr)
{
  int rc;
  char e_ident[EI_NIDENT];

  int fd = open(name, O_RDONLY);
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

  // Find ELF interpreter
  int i;
  Elf64_Phdr phdr;
  int phoff = elf_hdr.e_phoff;

  lseek(fd, phoff, SEEK_SET);
  for (i = 0; i < elf_hdr.e_phnum; i++)
  {
    assert(i < elf_hdr.e_phnum);
    rc = read(fd, &phdr, sizeof(phdr)); // Read consecutive program headers
    assert(rc == sizeof(phdr));
    if (phdr.p_type == PT_INTERP)
      break;
  }

  lseek(fd, phdr.p_offset, SEEK_SET); // Point to beginning of elf interpreter
  assert(phdr.p_filesz < MAX_ELF_INTERP_SZ);
  rc = read(fd, elf_interpreter, phdr.p_filesz);
  assert(rc == phdr.p_filesz);

  DLOG(INFO, "Interpreter: %s\n", elf_interpreter);
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
  RETURN_TO_UPPER_HALF();
  return ret;
}

// Sets up lower-half info struct for the upper half to read from. Returns 0
// on success, -1 otherwise
int Loader::setupLowerHalfInfo()
{
  // lhInfo.lhSbrk = (void *)&sbrkWrapper;
  // lhInfo.lhMmap = (void *)&mmapWrapper;
  // lhInfo.lhMunmap = (void *)&munmapWrapper;
  // lhInfo.lhDlsym = (void *)&lhDlsym;
  // lhInfo.lhMmapListFptr = (void *)&getMmappedList;
  // lhInfo.uhEndofHeapFptr = (void *)&getEndOfHeap;
  // lhInfo.getFatCubinHandle = (void *)&fatHandle;
  if (syscall(SYS_arch_prctl, ARCH_GET_FS, &lhInfo.lhFsAddr) < 0)
  {
    DLOG(ERROR, "Could not retrieve lower half's fs. Error: %s. Exiting...\n", strerror(errno));
    return -1;
  }
  // FIXME: We'll just write out the lhInfo object to a file; the upper half
  // will read this file to figure out the wrapper addresses. This is ugly
  // but will work for now.
  int rc = writeLhInfoToFile();
  if (rc < 0)
  {
    DLOG(ERROR, "Error writing address of lhinfo to file. Exiting...\n");
    return -1;
  }
  return 0;
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

#include "ld.h"
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <cstring>
#include "global.hpp"

Elf64_Addr LD::get_interpreter_entry(const char* ld_name)
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

unsigned long LD::map_elf_interpreter_load_segment(void* startAddr, int fd, Elf64_Ehdr* ehdr, Elf64_Phdr* phdr)
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

void* LD::load_elf_interpreter(void* startAddr, const char* elf_interpreter, DynObjInfo& info)
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

  unsigned long baseAddr = map_elf_interpreter_load_segment(startAddr, ld_so_fd, ehdr, phdr);

  info.set_phnum(elf_hdr.e_phnum);
  info.set_phdr((VA)baseAddr + elf_hdr.e_phoff);
  return (void*)baseAddr;
}
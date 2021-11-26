#include "trampoline.h"
#include "global.hpp"
#include <assert.h>
#include <elf.h>
#include <fcntl.h>
#include <unistd.h>

off_t Trampoline::getSymbolOffset(int fd, string ldname, string symbol)
{
  int i;
  ssize_t rc;
  Elf64_Shdr sect_hdr;
  Elf64_Shdr symtab;
  Elf64_Sym symtab_entry;
  // FIXME: This needs to be dynamic
  char strtab[100000];

  int symtab_found = 0;

  // Reset fd to beginning and parse file header
  lseek(fd, 0, SEEK_SET);
  Elf64_Ehdr elf_hdr;
  rc = read(fd, &elf_hdr, sizeof(elf_hdr));
  assert(rc == sizeof(elf_hdr));

  // Get start of symbol table and string table
  Elf64_Off shoff = elf_hdr.e_shoff;
  lseek(fd, shoff, SEEK_SET);
  for (i = 0; i < elf_hdr.e_shnum; i++) {
    rc = read(fd, &sect_hdr, sizeof sect_hdr);
    assert(rc == sizeof(sect_hdr));
    if (sect_hdr.sh_type == SHT_SYMTAB) {
      symtab       = sect_hdr;
      symtab_found = 1;
    }
    if (sect_hdr.sh_type == SHT_STRTAB) {
      int fd2 = open(ldname.c_str(), O_RDONLY);
      lseek(fd2, sect_hdr.sh_offset, SEEK_SET);
      if (sect_hdr.sh_size > sizeof(strtab)) {
        DLOG(ERROR, "sect_hdr.sh_size =  %zu, sizeof(strtab) = %zu\n", sect_hdr.sh_size, sizeof(strtab));
        assert(0);
      }
      assert(sect_hdr.sh_size = read(fd2, strtab, sect_hdr.sh_size));
      close(fd2);
    }
  }

  if (!symtab_found) {
    DLOG(ERROR, "Failed to find symbol table in %s\n", ldname);
    return -1;
  }

  // Move to beginning of symbol table
  lseek(fd, symtab.sh_offset, SEEK_SET);
  for (; lseek(fd, 0, SEEK_CUR) - symtab.sh_offset < symtab.sh_size;) {
    rc = read(fd, &symtab_entry, sizeof symtab_entry);
    assert(rc == sizeof(symtab_entry));
    if (strcmp(strtab + symtab_entry.st_name, symbol.c_str()) == 0) {
      // found address as offset from base address
      return symtab_entry.st_value;
    }
  }
  DLOG(ERROR, "Failed to find symbol (%s) in %s\n", symbol, ldname);
  return -1;
}

// Returns 0 on success, -1 on failure
int Trampoline::insertTrampoline(void* from_addr, void* to_addr)
{
  int rc;
#if defined(__x86_64__)
  unsigned char asm_jump[] = {// mov    $0x1234567812345678,%rax
                              0x48, 0xb8, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12,
                              // jmpq   *%rax
                              0xff, 0xe0};
  // Beginning of address in asm_jump:
  const int addr_offset = 2;
#elif defined(__i386__)
  static unsigned char asm_jump[] = {
      0xb8, 0x78, 0x56, 0x34, 0x12, // mov    $0x12345678,%eax
      0xff, 0xe0                    // jmp    *%eax
  };
  // Beginning of address in asm_jump:
  const int addr_offset = 1;
#else
#error "Architecture not supported"
#endif

  void* page_base    = (void*)ROUND_DOWN(from_addr);
  size_t page_length = PAGE_SIZE;
  if ((VA)from_addr + sizeof(asm_jump) - (VA)page_base > PAGE_SIZE) {
    // The patching instructions cross page boundary. View page as double size.
    page_length = 2 * PAGE_SIZE;
  }

  // Temporarily add write permissions
  rc = mprotect(page_base, page_length, PROT_READ | PROT_WRITE | PROT_EXEC);
  if (rc < 0) {
    DLOG(ERROR, "mprotect failed for %p at %zu. Error: %s\n", page_base, page_length, strerror(errno));
    return -1;
  }

  // Now, do the patching
  memcpy(from_addr, asm_jump, sizeof(asm_jump));
  memcpy((VA)from_addr + addr_offset, (void*)&to_addr, sizeof(&to_addr));

  // Finally, remove the write permissions
  rc = mprotect(page_base, page_length, PROT_READ | PROT_EXEC);
  if (rc < 0) {
    DLOG(ERROR, "mprotect failed: %s\n", strerror(errno));
    return -1;
  }
  return rc;
}

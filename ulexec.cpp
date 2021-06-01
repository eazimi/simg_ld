#include "ulexec.h"
#include <string.h>
#include <iostream>
#include "util.hpp"
#include <fstream>
#include <sys/stat.h>

#define ROUNDUP(x, y) ((((x) + ((y)-1)) / (y)) * (y))
#define ALIGN(k, v) (((k) + ((v)-1)) & (~((v)-1)))
#define ALIGNDOWN(k, v) ((unsigned long)(k) & (~((unsigned long)(v)-1)))
#define ALLOCATE(size) \
	Util::linux_mmap(0, (size), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)

#define JMP_ADDR(x) asm("\tjmp  *%0\n" :: "r" (x))
#define SET_STACK(x) asm("\tmovq %0, %%rsp\n" :: "r"(x))

#define HALF_G 0x1FCCA000 // 500 MB

typedef struct saved_block
{
	int size;
	int cnt;
	char *block;
} s_saved_block;

/* call with argc as positive value for main's argv,
 * call with argc == 0 for env. */
void *CUlexec::save_argv(int argc, char **argv)
{
	void *r = nullptr;
	int len = 0;

	if (argc > 0)
	{
		for (auto i = 0; i < argc; ++i)
			len += strlen(argv[i]) + 1;
	}
	else
	{
		argc = 0;
		char **p = argv;
		while (*p)
		{
			len += strlen(*p) + 1;
			++p; /* move past ASCII Nul */
			++argc;
		}
	}

	r = ALLOCATE(sizeof(s_saved_block));
	((s_saved_block *)r)->size = len;
	((s_saved_block *)r)->cnt = argc;
	((s_saved_block *)r)->block = (char *)ALLOCATE(len);

	/* Do it this way because the values of argv[] may not actually
	 * exist as contiguous strings.  We will make them contiguous. */
	auto str = ((s_saved_block *)r)->block;
	for (auto i = 0; i < argc; i++)
	{
		auto j = 0;
		for (; argv[i][j]; ++j)
			str[j] = argv[i][j];
		str[j] = '\0';
		str += (j + 1);
	}

	return r;
}

void *CUlexec::save_elfauxv(char **envp)
{
	void *r;
	unsigned long *p;
	Elf64_auxv_t *q;

	p = (unsigned long *)envp;
	while (*p != 0)
		++p;
	++p; /* skip null word after env */

	int cnt = 0;
	for (q = (Elf64_auxv_t *)p; q->a_type != AT_NULL; ++q)
		++cnt;

	++cnt; /* The AT_NULL final entry */

	r = ALLOCATE(sizeof(s_saved_block));
	auto size = (((s_saved_block *)r)->size = sizeof(*q) * cnt);
	((s_saved_block *)r)->cnt = cnt;
	((s_saved_block *)r)->block = (char *)ALLOCATE(size);

	auto dest = ((s_saved_block *)r)->block;
	Util::memcpy((void *)dest, (void *)p, size);

	return r;
}

void CUlexec::release_args(void *args)
{
	auto args_ = (s_saved_block *)args;
	auto block = (void *)args_->block;
	auto block_size = args_->size;
	auto arg_size = sizeof(*args_);

	Util::linux_munmap(block, block_size);
	Util::linux_munmap(args, arg_size);
}

void CUlexec::ulexec(int ac, char **av, char **env)
{
	auto trim_args = 1;
	// auto trim_args = 0;
	char file_to_map[MAX_PATH];
	// strcpy(file_to_map, (char *)"/usr/bin/ls");
	strcpy(file_to_map, av[1]);

	unsigned long mapped_sz;
	void *mapped;

	mapped = map_file(file_to_map, &mapped_sz);

	Elf64_Ehdr *elf_ehdr;
	elf_ehdr = (Elf64_Ehdr *)mapped;

	Elf64_Phdr *phdr;
	phdr = (Elf64_Phdr *)((unsigned long)elf_ehdr + elf_ehdr->e_phoff);

	int how_to_map = 0;
	for (int i = 0; i < elf_ehdr->e_phnum; ++i)
	{
		if (phdr[i].p_type == PT_LOAD && phdr[i].p_vaddr == 0)
		{
			how_to_map = 1; /* map it anywhere, like ld.so, or PIC code. */
			break;
		}
	}

	Elf64_Ehdr *ldso_ehdr;
	void *entry_point;
	void *offset = Util::linux_mmap(nullptr, HALF_G, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	// offset = nullptr;
	entry_point = load_elf((long int)offset, mapped, how_to_map, &elf_ehdr, &ldso_ehdr);
	Util::linux_munmap(mapped, mapped_sz);

	struct saved_block *argvb, *envb, *elfauxvb;
	argvb = (s_saved_block *)save_argv(ac - trim_args, &av[trim_args]);
	envb = (s_saved_block *)save_argv(0, env);
	elfauxvb = (s_saved_block *)save_elfauxv(env);

	void *stack_bottom;
	stack_bottom = stack_setup(argvb, envb, elfauxvb, elf_ehdr, ldso_ehdr);

	SET_STACK(stack_bottom);
	JMP_ADDR(entry_point);

	// while (true);
}

/* Returns value for %rsp, the new "bottom of the stack */
void *CUlexec::stack_setup(void *args, void *envp, void *auxvp, Elf64_Ehdr *ehdr, Elf64_Ehdr *ldso)
{
	auto args_  = (s_saved_block*) args;
	auto envp_  = (s_saved_block*) envp;
	auto auxvp_ = (s_saved_block*) auxvp;

	Elf64_auxv_t *aux = nullptr, *excfn = nullptr;
	char **av, **ev;
	char *addr, *str, *rsp;
	unsigned long *ptr;
	int i, j;
	char newstack[16384];

	/* Align new stack. */
	rsp = (char *)ALIGN(((unsigned long)&newstack[150]), 16);

	/* 
	 * After returning from
	 * stack_setup(), don't do anything that uses the call stack: that
	 * will roach this newly-constructed stack.
	 */

	ptr = (unsigned long *)rsp;

	*ptr++ = args_->cnt; /* set argc */
	av = (char **)ptr;
	ptr += args_->cnt; /* skip over argv[] */
	*ptr++ = 0;

	ev = (char **)ptr;
	ptr += envp_->cnt; /* skip over envp[] */
	*ptr++ = 0;

	aux = (Elf64_auxv_t *)ptr;

	ptr = (unsigned long *)ROUNDUP((unsigned long)ptr + auxvp_->size, sizeof(unsigned long));

	/* copy ELF auxilliary vector table */
	addr = (char *)aux;
	for (j = 0; j < auxvp_->size; ++j)
		addr[j] = auxvp_->block[j];

	/* Fix up a few entries: kernel will have set up the AUXV
	 * for the user-land exec program, mapped in at a low address.
	 * need to fix up a few AUXV entries for the "real" program. */
	for (i = 0; i < auxvp_->cnt; ++i)
	{
		switch (aux[i].a_type)
		{
		case AT_PHDR:
			aux[i].a_un.a_val = (unsigned long)((char *)ehdr + ehdr->e_phoff);
			break;
		case AT_PHNUM:
			aux[i].a_un.a_val = ehdr->e_phnum;
			break;
		case AT_BASE:
			aux[i].a_un.a_val = (unsigned long)ldso;
			break;
		case AT_ENTRY:
			aux[i].a_un.a_val = (unsigned long)ehdr->e_entry;
			break;
#ifdef AT_EXECFN
		case AT_EXECFN:
			excfn = &(aux[i]);
			break;
#endif
		}
	}

	*ptr++ = 0;

	/* Copy argv strings onto stack */
	addr = (char *)ptr;
	str = args_->block;

	for (i = 0; i < args_->cnt; ++i)
	{
		av[i] = addr;
		for (j = 0; *str; ++j)
			*addr++ = *str++;
		*addr++ = *str++; /* ASCII Nul */
	}

	ptr = (unsigned long *)ROUNDUP((unsigned long)addr, sizeof(unsigned long));
	*ptr++ = 0;

	/* Copy envp strings onto stack */
	addr = (char *)ptr;
	str = envp_->block;

	for (i = 0; i < envp_->cnt; ++i)
	{
		ev[i] = addr;
		for (j = 0; *str; ++j)
			*addr++ = *str++;
		*addr++ = *str++; /* ASCII Nul */
	}

	ptr = (unsigned long *)ROUNDUP((unsigned long)addr, sizeof(unsigned long));
	*ptr++ = 0;

	/* Executable name at top of stack */
	if (excfn)
	{
		addr = (char *)ptr;
		str = args_->block;
		excfn->a_un.a_val = (unsigned long)addr;
		for (j = 0; *str; ++j)
			*addr++ = *str++;
		*addr++ = *str++; /* ASCII Nul */

		ptr = (unsigned long *)ROUNDUP((unsigned long)addr, sizeof(unsigned long));
		*ptr = 0;
	}

	release_args(args);
	release_args(envp);
	release_args(auxvp);

	return ((void *)rsp);
}

void *CUlexec::map_file(char *file_to_map, unsigned long *sz)
{
	struct stat sb;
	void *mapped;

	if (0 > Util::linux_stat(file_to_map, &sb))
	{
		Util::error_msg("map_file stat() failed ");
		Util::linux_exit(1);
	}

	*sz = sb.st_size;

	mapped = Util::linux_mmap(nullptr, sb.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (mapped == (void *)-1)
	{
		Util::error_msg("map_file mmap() failed ");
		Util::linux_exit(1);
	}

	Util::copy_in(file_to_map, mapped);

	return mapped;
}

void *CUlexec::load_elf(unsigned long offset, void *mapped, int anywhere, Elf64_Ehdr **elf_ehdr, Elf64_Ehdr **ldso_ehdr)
{
	Elf64_Ehdr *hdr;
	Elf64_Phdr *pdr, *interp = nullptr;
	void *text_segment = nullptr;
	void *entry_point = nullptr;
	unsigned long initial_vaddr = 0;
	unsigned long brk_addr = 0;
	char buf[128];
	unsigned int mapflags = MAP_PRIVATE | MAP_ANONYMOUS;

	if (!anywhere)
		mapflags |= MAP_FIXED;

	/* Just addresses in mapped-in file. */
	hdr = (Elf64_Ehdr *)mapped;
	pdr = (Elf64_Phdr *)(mapped + hdr->e_phoff);

	entry_point = (void *)hdr->e_entry;
	bool first_time = true;

	for (int i = 0; i < hdr->e_phnum; ++i, ++pdr)
	{
		unsigned int protflags = 0;
		unsigned long map_addr = 0, rounded_len, k;
		unsigned long unaligned_map_addr = 0;
		void *segment;

		if (pdr->p_type == 0x03) /* PT_INTERP */
		{
			interp = pdr;
			continue;
		}

		if (pdr->p_type != PT_LOAD) /* Segment not "loadable" */
			continue;

		if (text_segment != 0 && anywhere)
		{
			unaligned_map_addr = (unsigned long)text_segment + ((unsigned long)pdr->p_vaddr - (unsigned long)initial_vaddr);
			map_addr = ALIGNDOWN((unsigned long)unaligned_map_addr, 0x1000);
			mapflags |= MAP_FIXED;
		}
		else if (!anywhere)
		{
			map_addr = ALIGNDOWN(pdr->p_vaddr, 0x1000);
		}
		else
		{
			map_addr = 0UL;
		}

		if (!anywhere && initial_vaddr == 0)
			initial_vaddr = pdr->p_vaddr;

		/* mmap() freaks out if you give it a non-multiple of pagesize */
		rounded_len = (unsigned long)pdr->p_memsz + ((unsigned long)pdr->p_vaddr % 0x1000);
		rounded_len = ROUNDUP(rounded_len, 0x1000);

		if (first_time)
		{
			map_addr = ALIGNDOWN((unsigned long)(map_addr + offset), 0x1000);
			first_time = false;
		}

		segment = Util::linux_mmap(
			(void *)map_addr,
			rounded_len,
			PROT_WRITE, mapflags, -1, 0);

		if (segment == (void *)-1)
		{
			Util::print_string(1, "Failed to mmap() ");
			Util::to_hex(pdr->p_memsz, buf);
			Util::print_string(1, buf);
			Util::print_string(1, " bytes at 0x");
			Util::to_hex(map_addr, buf);
			Util::print_string(1, buf);
			Util::print_string(1, "\n");
			Util::linux_exit(3);
		}

		Util::memcpy(
			!anywhere ? (void *)pdr->p_vaddr : (void *)((unsigned long)segment + ((unsigned long)pdr->p_vaddr % 0x1000)),
			mapped + pdr->p_offset,
			pdr->p_filesz);

		if (!text_segment)
		{
			*elf_ehdr = (Elf64_Ehdr *)segment;
			text_segment = segment;
			initial_vaddr = pdr->p_vaddr;
			if (anywhere)
				entry_point = (void *)((unsigned long)entry_point - (unsigned long)pdr->p_vaddr + (unsigned long)text_segment);
		}

		if (pdr->p_flags & PF_R)
			protflags |= PROT_READ;
		if (pdr->p_flags & PF_W)
			protflags |= PROT_WRITE;
		if (pdr->p_flags & PF_X)
			protflags |= PROT_EXEC;

		Util::linux_mprotect(segment, rounded_len, protflags);

		k = pdr->p_vaddr + pdr->p_memsz;
		if (k > brk_addr)
			brk_addr = k;
	}

	if (interp)
	{
		Elf64_Ehdr *junk_ehdr = nullptr;
		unsigned long sz_dummy;
		void *interp_mapped = map_file(&(((char *)mapped)[interp->p_offset]), &sz_dummy);
		void *offset = Util::linux_mmap(nullptr, HALF_G, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		// offset = nullptr;
		entry_point = load_elf((long int)offset, interp_mapped, 1, ldso_ehdr, &junk_ehdr);
	}

	if (!anywhere)
		Util::linux_brk(ROUNDUP(brk_addr, 0x1000));

	return (void *)entry_point;
}

void CUlexec::unmap(const char *progName)
{
	std::ifstream infile("/proc/self/maps");
	std::string line;
	while (std::getline(infile, line))
	{
		size_t posProgName = line.find(progName);
		size_t poslibdl = line.find("libdl");
		size_t posld1 = line.find("/usr/lib/ld-");
		size_t posld2 = line.find("/lib64/ld-");
		size_t poslibc = line.find("libc");

		if ((posProgName != std::string::npos) || (poslibdl != std::string::npos) || (posld1 != std::string::npos) ||
			(posld2 != std::string::npos) || (poslibc != std::string::npos))
		{
			auto spacePos = line.find(' ');
			auto range = line.substr(0, spacePos);
			auto dashPos = range.find('-');
			auto first = range.substr(0, dashPos);
			auto second = range.substr(++dashPos, range.size() - dashPos);
			unsigned long low = strtoul(first.c_str(), nullptr, 0x10);
			unsigned long high = strtoul(second.c_str(), nullptr, 0x10);
			auto ret = Util::linux_munmap((void *)low, high - low);
			std::cout << ret << ", no. of pages: " << (float)((high - low) / 4096) << std::endl;
		}
	}
	infile.close();
}

void CUlexec::process_image_backup(unsigned long low, unsigned long high)
{
#define PBPATH "process_image_backup.tmp"
	int fd = Util::linux_open(PBPATH, 01102, 0666);
	Util::linux_write(fd, (void *)low, high - low);
	Util::linux_close(fd);
}

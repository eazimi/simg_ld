#include <elf.h>

class CUlexec
{
public:
    explicit CUlexec() = default;
    void ulexec(int ac, char **av, char **env);

private:
    void unmap(const char *progName);
    void process_image_backup(unsigned long low, unsigned long high);
    void *map_file(char *file_to_map, unsigned long *sz);
    void *load_elf(unsigned long offset, void *mapped, int anywhere, Elf64_Ehdr **elf_ehdr, Elf64_Ehdr **ldso_ehdr);
    void *save_argv(int argc, char **argv);
    void *save_elfauxv(char **envp);
    void release_args(void *args);
    void *stack_setup(void *args, void *envp, void *auxvp, Elf64_Ehdr *ehdr, Elf64_Ehdr *ldso);
};
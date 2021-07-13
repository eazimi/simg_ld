#ifndef LOADER_GLOBAL_FUNCS_HPP
#define LOADER_GLOBAL_FUNCS_HPP

#include <cstdint>
#include <link.h>
#include <fcntl.h>

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
static ElfW(auxv_t) *get_auxv_addr(const char **env)
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

#endif
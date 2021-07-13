#ifndef LOADER_GLOBAL_FUNCS_HPP
#define LOADER_GLOBAL_FUNCS_HPP

#include <cstdint>
#include <link.h>

static void *GET_ARGC_ADDR(const void *stackEnd)
{
    return (void *)((uintptr_t)(stackEnd) + sizeof(uintptr_t));
}

// Returns pointer to argv[0], given a pointer to end of stack
static void *GET_ARGV_ADDR(const void *stackEnd)
{
    return (void *)((unsigned long)(stackEnd) + 2 * sizeof(uintptr_t));
}

// Returns pointer to env[0], given a pointer to end of stack
static void *GET_ENV_ADDR(char **argv, int argc)
{
    return (void *)&argv[argc + 1];
}

// Returns a pointer to aux vector, given a pointer to the environ vector
// on the stack
static ElfW(auxv_t) * GET_AUXV_ADDR(const char **env)
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

#endif
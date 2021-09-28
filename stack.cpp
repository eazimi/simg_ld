#include "stack.h"
#include <fcntl.h>

Stack::Stack() {}

// Returns the [stack] area by reading the proc maps
Area&& Stack::getStackRegion()
{
  Area area;
  int mapsfd = open("/proc/self/maps", O_RDONLY);
  while (readMapsLine(mapsfd, &area)) {
    if (strstr(area.name, "[stack]") && area.endAddr >= (VA)&area) {
      break;
    }
  }
  close(mapsfd);
  return std::move(area);
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

void* Stack::get_argc_addr(const void* stackEnd) const
{
  return (void*)((uintptr_t)(stackEnd) + sizeof(uintptr_t));
}

// Returns pointer to argv[0], given a pointer to end of stack
void* Stack::get_argv_addr(const void* stackEnd) const
{
  return (void*)((unsigned long)(stackEnd) + 2 * sizeof(uintptr_t));
}

// Returns pointer to env[0], given a pointer to end of stack
void* Stack::get_env_addr(char** argv, int argc) const
{
  return (void*)&argv[argc + 1];
}

// Returns a pointer to aux vector, given a pointer to the environ vector on the stack
ElfW(auxv_t) * Stack::get_auxv_addr(const char** env) const
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

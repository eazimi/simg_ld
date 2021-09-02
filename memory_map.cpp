#include "memory_map.h"

#include <fstream>
#include <string.h>
#include <cstring>
#include <array>
#include <sys/mman.h>

// abort with a message if `expr' is false
#define CHECK(expr)                                                                                                    \
  if (not(expr)) {                                                                                                     \
    fprintf(stderr, "CHECK FAILED: %s:%d: %s\n", __FILE__, __LINE__, #expr);                                           \
    abort();                                                                                                           \
  } else                                                                                                               \
    ((void)0)

std::vector<VmMap>
MemoryMap::get_memory_map(pid_t pid)
{
  std::vector<VmMap> ret;
#if defined __linux__
  /* Open the actual process's proc maps file and create the memory_map_t */
  /* to be returned. */
  std::string path = std::string("/proc/") + std::to_string(pid) + "/maps";
  std::ifstream fp;
  fp.rdbuf()->pubsetbuf(nullptr, 0);
  fp.open(path);
  if (not fp) {
    std::perror("open failed");
    std::fprintf(stderr, "Cannot open %s to investigate the memory map of the process.\n", path.c_str());
    abort();
  }

  /* Read one line at the time, parse it and add it to the memory map to be returned */
  std::string sline;
  while (std::getline(fp, sline)) {
    /**
     * The lines that we read have this format: (This is just an example)
     * 00602000-00603000 rw-p 00002000 00:28 1837264                            <complete-path-to-file>
     */
    char* line = &sline[0];

    /* Tokenize the line using spaces as delimiters and store each token in lfields array. We expect 5 tokens for 6
     * fields */
    char* saveptr = nullptr; // for strtok_r()
    std::array<char*, 6> lfields;
    lfields[0] = strtok_r(line, " ", &saveptr);

    int i;
    for (i = 1; i < 6 && lfields[i - 1] != nullptr; i++) {
      lfields[i] = strtok_r(nullptr, " ", &saveptr);
    }

    /* Check to see if we got the expected amount of columns */
    if (i < 6) {
      std::fprintf(stderr, "The memory map apparently only supplied less than 6 columns. Recovery impossible.\n");
      abort();
    }

    /* Ok we are good enough to try to get the info we need */
    /* First get the start and the end address of the map   */
    const char* tok = strtok_r(lfields[0], "-", &saveptr);
    if (tok == nullptr) {
      std::fprintf(stderr,
                   "Start and end address of the map are not concatenated by a hyphen (-). Recovery impossible.\n");
      abort();
    }

    VmMap memreg;
    char* endptr;
    memreg.start_addr = std::strtoull(tok, &endptr, 16);
    /* Make sure that the entire string was an hex number */
    CHECK(*endptr == '\0');

    tok = strtok_r(nullptr, "-", &saveptr);
    CHECK(tok != nullptr);

    memreg.end_addr = std::strtoull(tok, &endptr, 16);
    /* Make sure that the entire string was an hex number */
    CHECK(*endptr == '\0');

    /* Get the permissions flags */
    CHECK(std::strlen(lfields[1]) >= 4);

    memreg.prot = 0;
    for (i = 0; i < 3; i++) {
      switch (lfields[1][i]) {
        case 'r':
          memreg.prot |= PROT_READ;
          break;
        case 'w':
          memreg.prot |= PROT_WRITE;
          break;
        case 'x':
          memreg.prot |= PROT_EXEC;
          break;
        default:
          break;
      }
    }
    if (memreg.prot == 0)
      memreg.prot |= PROT_NONE;

    memreg.flags = 0;
    if (lfields[1][3] == 'p') {
      memreg.flags |= MAP_PRIVATE;
    } else {
      memreg.flags |= MAP_SHARED;
      if (lfields[1][3] != 's')
        fprintf(stderr,
                "The protection is neither 'p' (private) nor 's' (shared) but '%s'. Let's assume shared, as on b0rken "
                "win-ubuntu systems.\nFull line: %s\n",
                lfields[1], line);
    }

    /* Get the offset value */
    memreg.offset = std::strtoull(lfields[2], &endptr, 16);
    /* Make sure that the entire string was an hex number */
    CHECK(*endptr == '\0');

    /* Get the device major:minor bytes */
    tok = strtok_r(lfields[3], ":", &saveptr);
    CHECK(tok != nullptr);

    memreg.dev_major = (char)strtoul(tok, &endptr, 16);
    /* Make sure that the entire string was an hex number */
    CHECK(*endptr == '\0');

    tok = strtok_r(nullptr, ":", &saveptr);
    CHECK(tok != nullptr);

    memreg.dev_minor = (char)std::strtoul(tok, &endptr, 16);
    /* Make sure that the entire string was an hex number */
    CHECK(*endptr == '\0');

    /* Get the inode number and make sure that the entire string was a long int */
    memreg.inode = strtoul(lfields[4], &endptr, 10);
    CHECK(*endptr == '\0');

    /* And finally get the pathname */
    if (lfields[5])
      memreg.pathname = lfields[5];

    ret.push_back(std::move(memreg));
  }

  fp.close();
#else
  std::fprintf(stderr, "Could not get memory map from process %lli\n", (long long int)pid);
  abort();
#endif
  return ret;
}
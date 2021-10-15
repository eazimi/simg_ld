#ifndef GLOBAL_HPP
#define GLOBAL_HPP

#include <cstring>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <link.h>
#include <sstream>
#include <stdio.h>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

using namespace std;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////    DEFINE - MACRO   //////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define LD_NAME "/lib64/ld-linux-x86-64.so.2"

constexpr unsigned MESSAGE_LENGTH = 512;

#define FILENAMESIZE 1024
#define MAX_ELF_INTERP_SZ 256

#define PAGE_SIZE 0x1000LL

#define GB1    0x40000000
#define MB1500 0x60000000
#define GB2    0x80000000
#define GB3    0xc0000000
#define GB4    0x100000000
#define GB5    0x140000000
#define MB5500 0x160000000

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

static const char* colors[] = {KNRM, KRED, KBLU, KGRN, KYEL};

#define DLOG(LOG_LEVEL, fmt, ...)                                                                                      \
  do {                                                                                                                 \
    fprintf(stderr, "%s[%s +%d]: " fmt KNRM, colors[LOG_LEVEL], __FILE__, __LINE__ __VA_OPT__(, ) __VA_ARGS__);        \
  } while (0)

// FIXME: 0x1000 is one page; Use sysconf(PAGESIZE) instead.
#define ROUND_DOWN(x) ((unsigned long long)(x) & ~(unsigned long long)(PAGE_SIZE - 1))
#define ROUND_UP(x) (((unsigned long long)(x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PAGE_OFFSET(x) ((x) & (PAGE_SIZE - 1))

#define ALIGN (PAGE_SIZE - 1)
#define ROUND_PG(x) (((x) + (ALIGN)) & ~(ALIGN))
#define TRUNC_PG(x) ((x) & ~(ALIGN))
#define PFLAGS(x) ((((x)&PF_R) ? PROT_READ : 0) | (((x)&PF_W) ? PROT_WRITE : 0) | (((x)&PF_X) ? PROT_EXEC : 0))
#define LOAD_ERR ((unsigned long)-1)

// TODO: This is very x86-64 specific; support other architectures??
#define eax rax
#define ebx rbx
#define ecx rcx
#define edx rax
#define ebp rbp
#define esi rsi
#define edi rdi
#define esp rsp
#define CLEAN_FOR_64_BIT_HELPER(args...) #args
#define CLEAN_FOR_64_BIT(args...) CLEAN_FOR_64_BIT_HELPER(args)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////    DATA STRUCTURE   //////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef char* VA; /* VA = virtual address */

// Based on the entries in /proc/<pid>/stat as described in `man 5 proc`
enum Procstat_t {
  PID = 1,
  COMM,  // 2
  STATE, // 3
  PPID,  // 4
  NUM_THREADS = 19,
  STARTSTACK  = 27
};

typedef union ProcMapsArea {
  struct {
    union {
      VA addr; // args required for mmap to restore memory area
      uint64_t __addr;
    };
    union {
      VA endAddr; // args required for mmap to restore memory area
      uint64_t __endAddr;
    };
    union {
      size_t size;
      uint64_t __size;
    };
    union {
      off_t offset;
      uint64_t __offset;
    };
    union {
      int prot;
      uint64_t __prot;
    };
    union {
      int flags;
      uint64_t __flags;
    };
    union {
      unsigned int long devmajor;
      uint64_t __devmajor;
    };
    union {
      unsigned int long devminor;
      uint64_t __devminor;
    };
    union {
      ino_t inodenum;
      uint64_t __inodenum;
    };

    uint64_t properties;

    char name[FILENAMESIZE];
  };
  char _padding[4096];
} ProcMapsArea;

typedef ProcMapsArea Area;

typedef struct {
  void* start;
  void* end;
} MemoryArea_t;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////    FUNCTIONS - VARIABLES   ///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static pid_t _parent_pid;

static void pause_run(std::string message)
{
  // std::cout << message << std::endl;
  // std::cout << "press a key to continue ..." << std::endl;
  // std::string str;
  // std::cin >> str;
}

/* Read non-null character, return null if EOF */
static char readChar(int fd)
{
  char c;
  int rc;

  do {
    rc = read(fd, &c, 1);
  } while (rc == -1 && errno == EINTR);
  if (rc <= 0)
    return 0;
  return c;
}

/* Read decimal number, return value and terminating character */
static char readDec(int fd, VA* value)
{
  char c;
  unsigned long int v = 0;

  while (1) {
    c = readChar(fd);
    if ((c >= '0') && (c <= '9'))
      c -= '0';
    else
      break;
    v = v * 10 + c;
  }
  *value = (VA)v;
  return c;
}

/* Read decimal number, return value and terminating character */
static char readHex(int fd, VA* virt_mem_addr)
{
  char c;
  unsigned long int v = 0;

  while (1) {
    c = readChar(fd);
    if ((c >= '0') && (c <= '9'))
      c -= '0';
    else if ((c >= 'a') && (c <= 'f'))
      c -= 'a' - 10;
    else if ((c >= 'A') && (c <= 'F'))
      c -= 'A' - 10;
    else
      break;
    v = v * 16 + c;
  }
  *virt_mem_addr = (VA)v;
  return c;
}

static bool getUnmmapAddressRange(string line, string token, pair<unsigned long, unsigned long>& addr_long)
{
  bool found     = false;
  auto pos       = line.rfind(token);
  auto token_len = token.length();
  if (pos != string::npos) {
    if (pos + token_len == line.length())
      found = true;
  }

  if (found) {
    auto addr_range_str   = line.substr(0, line.find(" "));
    char* addr_range_carr = (char*)addr_range_str.c_str();
    auto dash_index       = strchr(addr_range_carr, '-');
    char start_addr_str[32];
    auto copy_len = dash_index - addr_range_carr;
    strncpy(start_addr_str, addr_range_carr, copy_len);
    start_addr_str[copy_len] = '\0';
    char* end_addr_str       = dash_index + 1;

    std::stringstream ss;
    ss << std::hex << start_addr_str;
    unsigned long start_addr_long;
    ss >> start_addr_long;
    ss.clear();
    ss << std::hex << end_addr_str;
    unsigned long end_addr_long;
    ss >> end_addr_long;

    addr_long.first  = start_addr_long;
    addr_long.second = end_addr_long;
  }

  return found;
}

static vector<pair<unsigned long, unsigned long>> getRanges(string maps_path, vector<string> tokens)
{
  filebuf fb;
  string line;
  vector<pair<unsigned long, unsigned long>> result;
  if (fb.open(maps_path, ios_base::in)) {
    istream is(&fb);
    while (getline(is, line)) {
      for (auto it : tokens) {
        pair<unsigned long, unsigned long> addr_long;
        auto found = getUnmmapAddressRange(line, it, addr_long);
        if (found)
          result.emplace_back(std::move(addr_long));
      }
    }
    fb.close();
  }
  return result;
}

static void print_mmapped_ranges(pid_t pid = -1)
{
  if (pid != -1)
    std::cout << ((pid == _parent_pid) ? "[PARENT], " : "[CHILD], ") << "printing mmaped regions ..." << std::endl;
  std::string maps_path = "/proc/self/maps";
  std::filebuf fb;
  std::string line;
  if (fb.open(maps_path, std::ios_base::in)) {
    std::istream is(&fb);
    while (std::getline(is, line))
      std::cout << line << std::endl;
    fb.close();
  }
}

static void write_mmapped_ranges(string file_label, pid_t pid = -1)
{
  stringstream ss;
  ss << "./log/" << pid << "_" << file_label << ".txt";
  ofstream ofs(ss.str(), ofstream::out);

  std::string maps_path = "/proc/self/maps";
  std::filebuf fb;
  std::string line;
  if (fb.open(maps_path, std::ios_base::in)) {
    std::istream is(&fb);
    while (std::getline(is, line))
      ofs << line << std::endl;
    fb.close();
  }
  ofs.close();
}

static long int str_parse_int(const char* str, const char* error_msg)
{
  stringstream ss;
  ss << error_msg << ": " << str;

  char* endptr;
  if (str == nullptr || str[0] == '\0')
    throw std::invalid_argument(ss.str());

  long int res = strtol(str, &endptr, 10);
  if (endptr[0] != '\0')
    throw std::invalid_argument(ss.str());

  return res;
}

static int readMapsLine(int mapsfd, Area* area)
{
  char c, rflag, sflag, wflag, xflag;
  int i;
  off_t offset;
  unsigned int long devmajor, devminor, inodenum;
  VA startaddr, endaddr;

  c = readHex(mapsfd, &startaddr);
  if (c != '-') {
    if ((c == 0) && (startaddr == 0))
      return (0);
    goto skipeol;
  }
  c = readHex(mapsfd, &endaddr);
  if (c != ' ')
    goto skipeol;
  if (endaddr < startaddr)
    goto skipeol;

  rflag = c = readChar(mapsfd);
  if ((c != 'r') && (c != '-'))
    goto skipeol;
  wflag = c = readChar(mapsfd);
  if ((c != 'w') && (c != '-'))
    goto skipeol;
  xflag = c = readChar(mapsfd);
  if ((c != 'x') && (c != '-'))
    goto skipeol;
  sflag = c = readChar(mapsfd);
  if ((c != 's') && (c != 'p'))
    goto skipeol;

  c = readChar(mapsfd);
  if (c != ' ')
    goto skipeol;

  c = readHex(mapsfd, (VA*)&offset);
  if (c != ' ')
    goto skipeol;
  area->offset = offset;

  c = readHex(mapsfd, (VA*)&devmajor);
  if (c != ':')
    goto skipeol;
  c = readHex(mapsfd, (VA*)&devminor);
  if (c != ' ')
    goto skipeol;
  c             = readDec(mapsfd, (VA*)&inodenum);
  area->name[0] = '\0';
  while (c == ' ')
    c = readChar(mapsfd);
  if (c == '/' || c == '[') { /* absolute pathname, or [stack], [vdso], etc. */
    i = 0;
    do {
      area->name[i++] = c;
      if (i == sizeof area->name)
        goto skipeol;
      c = readChar(mapsfd);
    } while (c != '\n');
    area->name[i] = '\0';
  }

  if (c != '\n')
    goto skipeol;

  area->addr    = startaddr;
  area->endAddr = endaddr;
  area->size    = endaddr - startaddr;
  area->prot    = 0;
  if (rflag == 'r')
    area->prot |= PROT_READ;
  if (wflag == 'w')
    area->prot |= PROT_WRITE;
  if (xflag == 'x')
    area->prot |= PROT_EXEC;
  area->flags = MAP_FIXED;
  if (sflag == 's')
    area->flags |= MAP_SHARED;
  if (sflag == 'p')
    area->flags |= MAP_PRIVATE;
  if (area->name[0] == '\0')
    area->flags |= MAP_ANONYMOUS;

  area->devmajor = devmajor;
  area->devminor = devminor;
  area->inodenum = inodenum;
  return (1);

skipeol:
  fprintf(stderr, "ERROR: readMapsLine*: bad maps line <%c", c);
  while ((c != '\n') && (c != '\0')) {
    c = readChar(mapsfd);
    printf("%c", c);
  }
  printf(">\n");
  abort();
  return 0; /* NOTREACHED : stop compiler warning */
}

static void* mmapWrapper(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
  length    = ROUND_UP(length);
  void* ret = mmap(addr, length, prot, flags, fd, offset);
  return ret;
}

#endif
#ifndef APP_LOADER_GLOBAL_HPP
#define APP_LOADER_GLOBAL_HPP

#include <link.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string>
#include <sstream>
#include <cstring>

using namespace std;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////    DEFINE - MACRO   //////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define FILENAMESIZE 1024
#define _1_GB    0x40000000
#define _3_GB    0xc0000000

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

static const char *colors[] = {KNRM, KRED, KBLU, KGRN, KYEL};

#define DLOG(LOG_LEVEL, fmt, ...)                                             \
    do                                                                        \
    {                                                                         \
        fprintf(stderr, "%s[%s +%d]: " fmt KNRM, colors[LOG_LEVEL], __FILE__, \
                __LINE__ __VA_OPT__(, ) __VA_ARGS__);                         \
    } while (0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////    DATA STRUCTURE   //////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef char *VA; /* VA = virtual address */

typedef union ProcMapsArea
{
    struct
    {
        union
        {
            VA addr; // args required for mmap to restore memory area
            uint64_t __addr;
        };
        union
        {
            VA endAddr; // args required for mmap to restore memory area
            uint64_t __endAddr;
        };
        union
        {
            size_t size;
            uint64_t __size;
        };
        union
        {
            off_t offset;
            uint64_t __offset;
        };
        union
        {
            int prot;
            uint64_t __prot;
        };
        union
        {
            int flags;
            uint64_t __flags;
        };
        union
        {
            unsigned int long devmajor;
            uint64_t __devmajor;
        };
        union
        {
            unsigned int long devminor;
            uint64_t __devminor;
        };
        union
        {
            ino_t inodenum;
            uint64_t __inodenum;
        };

        uint64_t properties;

        char name[FILENAMESIZE];
    };
    char _padding[4096];
} ProcMapsArea;

typedef ProcMapsArea Area;

typedef struct _MemoryArea
{
  void *start;
  void *end;
} MemoryArea_t;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////    FUNCTIONS   ///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Read non-null character, return null if EOF */
static char readChar(int fd)
{
    char c;
    int rc;

    do
    {
        rc = read(fd, &c, 1);
    } while (rc == -1 && errno == EINTR);
    if (rc <= 0)
        return 0;
    return c;
}

/* Read decimal number, return value and terminating character */
static char readDec(int fd, VA *value)
{
    char c;
    unsigned long int v = 0;

    while (1)
    {
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
static char readHex(int fd, VA *virt_mem_addr)
{
    char c;
    unsigned long int v = 0;

    while (1)
    {
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

static bool getUnmmapAddressRange(string line, string token, pair<unsigned long, unsigned long> &addr_long)
{
    bool found = false;
    auto pos = line.rfind(token);
    auto token_len = token.length();
    if (pos != string::npos)
    {
        if (pos + token_len == line.length())
            found = true;
    }

    if (found)
    {
        auto addr_range_str = line.substr(0, line.find(" "));
        char *addr_range_carr = (char *)addr_range_str.c_str();
        auto dash_index = strchr(addr_range_carr, '-');
        char start_addr_str[32];
        auto copy_len = dash_index - addr_range_carr;
        strncpy(start_addr_str, addr_range_carr, copy_len);
        start_addr_str[copy_len] = '\0';
        char *end_addr_str = dash_index + 1;

        std::stringstream ss;
        ss << std::hex << start_addr_str;
        unsigned long start_addr_long;
        ss >> start_addr_long;
        ss.clear();
        ss << std::hex << end_addr_str;
        unsigned long end_addr_long;
        ss >> end_addr_long;

        addr_long.first = start_addr_long;
        addr_long.second = end_addr_long;
    }

    return found;
}

#endif
#include "app_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iostream>

using namespace std;

void AppLoader::printMMappedRanges()
{
  std::string maps_path = "/proc/self/maps";
  std::filebuf fb;
  std::string line;
  if (fb.open(maps_path, std::ios_base::in))
  {
    std::istream is(&fb);
    while (std::getline(is, line))
      std::cout << line << std::endl;
    fb.close();
  }
}

void AppLoader::memUnmapRanges()
{
  const string maps_path = "/proc/self/maps";
  const string SIMG_LD = "/simg_ld";
  const string HEAP = "[heap]";
  const string STACK = "[stack]";
  const string VVAR = "[vvar]";
  const string VDSO = "[vdso]";
  const string VSYSCALL = "[vsyscall]";

  filebuf fb;
  string line;
  if (fb.open(maps_path, ios_base::in))
  {
    istream is(&fb);    
    while (getline(is, line))
    {
      bool found = false;
      
      // HEAP, STACK, VVAR, VDSO, VSYSCALL
      auto pos = line.rfind(SIMG_LD);
      auto str_len = SIMG_LD.length();
      if(pos != string::npos)
      {
        if(pos+str_len == line.length())
          found = true;
      }

      if (found)
      {
        auto addr_range_str = line.substr(0, line.find(" "));
        char *addr_range_carr = (char *)addr_range_str.c_str();
        auto dash_index = strchr(addr_range_carr, '-');
        char start_addr_str[32];
        auto copy_len = dash_index-addr_range_carr;
        strncpy(start_addr_str, addr_range_carr, copy_len);
        start_addr_str[copy_len] = '\0';
        char *end_addr_str = dash_index+1;

        std::stringstream ss; 
        ss << std::hex << start_addr_str;
        unsigned long start_addr_long;
        ss >> start_addr_long;
        ss.clear();
        ss << std::hex << end_addr_str;
        unsigned long end_addr_long;
        ss >> end_addr_long;

        auto ret = munmap((void *)start_addr_long, end_addr_long-start_addr_long);
        if(ret != 0)
        {
          cout << "munmap was not successful: " << strerror(errno) << " # " << std::hex << start_addr_long << " - " << std::hex << end_addr_long << endl;
          cout << line << endl;
        }
      }
    }
    fb.close();
  }
}

void AppLoader::getReservedMemRange(std::pair<void *, void *> &range)
{
  Area area;
  bool found = false;
  int mapsfd = open("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0)
  {
    DLOG(ERROR, "Failed to open proc maps\n");
    return;
  }
  while (readMapsLine(mapsfd, &area))
  {
    if (strstr(area.name, "[stack]"))
    {
      found = true;
      break;
    }
  }
  close(mapsfd);

  // if (found && (g_range == nullptr))
  if (found)
  {
    reserved_area->start = (VA)area.addr - _3_GB;
    reserved_area->end = (VA)area.addr - _1_GB;
    range.first = reserved_area->start;
    range.second = reserved_area->end;
  }
}

int AppLoader::readMapsLine(int mapsfd, Area *area)
{
  char c, rflag, sflag, wflag, xflag;
  int i;
  off_t offset;
  unsigned int long devmajor, devminor, inodenum;
  VA startaddr, endaddr;

  c = readHex(mapsfd, &startaddr);
  if (c != '-')
  {
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

  c = readHex(mapsfd, (VA *)&offset);
  if (c != ' ')
    goto skipeol;
  area->offset = offset;

  c = readHex(mapsfd, (VA *)&devmajor);
  if (c != ':')
    goto skipeol;
  c = readHex(mapsfd, (VA *)&devminor);
  if (c != ' ')
    goto skipeol;
  c = readDec(mapsfd, (VA *)&inodenum);
  area->name[0] = '\0';
  while (c == ' ')
    c = readChar(mapsfd);
  if (c == '/' || c == '[')
  { /* absolute pathname, or [stack], [vdso], etc. */
    i = 0;
    do
    {
      area->name[i++] = c;
      if (i == sizeof area->name)
        goto skipeol;
      c = readChar(mapsfd);
    } while (c != '\n');
    area->name[i] = '\0';
  }

  if (c != '\n')
    goto skipeol;

  area->addr = startaddr;
  area->endAddr = endaddr;
  area->size = endaddr - startaddr;
  area->prot = 0;
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
  while ((c != '\n') && (c != '\0'))
  {
    c = readChar(mapsfd);
    printf("%c", c);
  }
  printf(">\n");
  abort();
  return 0; /* NOTREACHED : stop compiler warning */
}
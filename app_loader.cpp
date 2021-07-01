#include "app_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <cstring>
#include <fstream>
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
    bool found = false;
    while (getline(is, line))
    {
      found = (line.find(SIMG_LD) != string::npos) || (line.find(HEAP) != string::npos) || (line.find(STACK) != string::npos) ||
              (line.find(VVAR) != string::npos) || (line.find(VDSO) != string::npos) || (line.find(VSYSCALL) != string::npos);
      if (found)
      {
        auto addr_range_str = line.substr(0, line.find(" "));
        auto dash_index = line.find('-');
        auto start_str = addr_range_str.substr(0, dash_index);
        auto end_str = addr_range_str.substr(dash_index+1);
        auto dash_index_last = line.find_last_of(" ");        
        // cout << "found: " << start_str << ", " << end_str << "    " << line.substr(dash_index_last+1) << endl;
        start_str += "0x";
        end_str += "0x";
        auto start_int = strtol(start_str.c_str(), nullptr , 0);
        auto end_int = strtol(end_str.c_str(), nullptr, 0);
        // cout << "address: " << start_int << ", " << end_int << endl;
        auto ret = munmap((void *)((unsigned long)start_int), (unsigned long)(end_int-start_int));
        if(ret != 0)
          cout << "munmap was not successful: " << strerror(errno) << endl;
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
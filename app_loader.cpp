#include "app_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <assert.h>

using namespace std;

AppLoader::AppLoader()
{ 
  reserved_area = std::make_unique<MemoryArea_t>();
  initialize();
}

void AppLoader::release_parent_memory_region()
{
  const string maps_path = "/proc/self/maps";
  vector<string> tokens{"/simg_ld", "[heap]", "[stack]", "[vvar]", "[vdso]"};
  vector<pair<unsigned long, unsigned long>> all_addr = getRanges(maps_path, tokens);
  for (auto it : all_addr)
  {
    auto ret = munmap((void *)it.first, it.second - it.first);
    if (ret != 0)
      cout << "munmap was not successful: " << strerror(errno) << " # " << std::hex << it.first << " - " << std::hex << it.second << endl;
  }
}

void AppLoader::get_reserved_memory_region(std::pair<void *, void *> &range)
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

void AppLoader::initialize()
{
  if (not std::getenv(SIMG_LD_ENV_SOCKET_FD))
    return;

  // Fetch socket from SIMG_LD_ENV_SOCKET_FD:
  const char *fd_env = std::getenv(SIMG_LD_ENV_SOCKET_FD);
  int fd = str_parse_int(fd_env, "Not a number in variable '" SIMG_LD_ENV_SOCKET_FD "'");

  // Check the socket type/validity:
  int type;
  socklen_t socklen = sizeof(type);
  assert((getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &socklen) == 0) && "Could not check socket type");
  stringstream ss;
  ss << "Unexpected socket type " << type;
  auto str = ss.str().c_str();
  assert((type == SOCK_SEQPACKET) && str);
}
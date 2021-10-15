#include "user_space.h"
#include <fcntl.h>
#include <tuple>
#include "global.hpp"

void UserSpace::reserve_mem_space(unsigned long relativeDistFromStack, unsigned long size)
{
  Area area;
  bool found = false;
  int mapsfd = open("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    DLOG(ERROR, "Failed to open proc maps\n");
    return;
  }
  while (readMapsLine(mapsfd, &area)) {
    if (strstr(area.name, "[stack]")) {
      found = true;
      break;
    }
  }
  close(mapsfd);

  void* startAddr = nullptr;
  if (found)
    startAddr = (VA)area.addr - relativeDistFromStack;

  void* spaceAddr =
      mmapWrapper(startAddr, size, PROT_READ | PROT_WRITE, MAP_GROWSDOWN | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (spaceAddr == MAP_FAILED) {
    DLOG(ERROR, "Failed to mmap region: %s\n", strerror(errno));
    startAddr_ = nullptr;
    size_ = 0;
    return;
  }

  startAddr_ = spaceAddr;
  size_ = size;

  cout << "reserved addr: " << std::hex << startAddr << endl;
}

void UserSpace::mmap_all_free_spaces()
{
  std::vector<pair<void*, void*>> mmaps_range {}; // start and end of a range
  Area area;
  int mapsfd     = open("/proc/self/maps", O_RDONLY);
  bool firstLine = true;
  pair<void*, void*> range; // start, end
  while (readMapsLine(mapsfd, &area)) {
    // todo: check if required to add this condition: (area.endAddr >= (VA)&area)
    if (firstLine) {
      get<0>(range) = area.endAddr; // start
      firstLine   = false;
      continue;
    }
    get<1>(range) = area.addr; // end
    mmaps_range.push_back(range);
    get<0>(range) = area.endAddr;
  }
  close(mapsfd);
  mmaps_range.pop_back();

  // auto mmaps_size = mmaps_range.size() - 1;
  for (auto r : mmaps_range) {
    auto start_mmap = (unsigned long)(r.first);
    auto length     = (unsigned long)(r.second) - start_mmap;
    if (length == 0)
      continue;
    void* mmap_ret = mmap((void*)start_mmap, length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (mmap_ret == MAP_FAILED) {
      DLOG(ERROR, "failed to lock the free spot. %s\n", strerror(errno));
      exit(-1);
    }
  }

  // ofstream ofs("./log/free_space_locked.txt", ofstream::out);
  // for (auto r : mmaps_range) {
  //   if ((unsigned long)r.second - (unsigned long)r.first > 0) {
  //     ofs << r.first << " - " << r.second << endl;
  //   }
  // }
  // ofs.close();
}

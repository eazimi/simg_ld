#include "user_space.h"
#include <fcntl.h>
#include "global.hpp"

user_space::user_space()
{
  stack_ = make_unique<Stack>();
  heap_  = make_unique<Heap>();
}

void* user_space::getStackEnd() const
{
  return stack_->getStackEnd();
}

void* user_space::createNewHeap(void* heapStartAddr) const
{
  return heap_->createNewHeap(heapStartAddr);
}

void* user_space::createNewStack(const DynObjInfo& info, void* stackStartAddr, int param_index, int param_count,
                                int socket_id) const
{
  return stack_->createNewStack(info, stackStartAddr, param_index, param_count, socket_id);
}

void user_space::reserve_mem_space(unsigned long size)
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
    startAddr = (VA)area.addr - GB3;

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
}

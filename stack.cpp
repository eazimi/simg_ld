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

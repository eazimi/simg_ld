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

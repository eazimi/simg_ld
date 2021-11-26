#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#include <iostream>
#include <vector>

struct VmMap {
  std::uint64_t start_addr;
  std::uint64_t end_addr;
  int prot;                     /* Memory protection */
  int flags;                    /* Additional memory flags */
  std::uint64_t offset;         /* Offset in the file/whatever */
  char dev_major;               /* Major of the device */
  char dev_minor;               /* Minor of the device */
  unsigned long inode;          /* Inode in the device */
  std::string pathname;         /* Path name of the mapped file */
};

class MemoryMap
{
private:
    /* data */
public:
    explicit MemoryMap() = default;
    ~MemoryMap() = default;

    std::vector<VmMap> get_memory_map(pid_t pid);    
};

#endif
#ifndef APP_H
#define APP_H

#include <memory>
#include "channel.hpp"
#include "global.hpp"

class App {
private:
  void handle_message() const;
  std::unique_ptr<MemoryArea_t> reserved_area;
  void init(const char* socket);
  unique_ptr<Channel> channel_;
  void release_parent_memory_region(vector<string> memlayout) const;

public:
  explicit App(const char* socket);
  void get_reserved_memory_region(std::pair<void*, void*>& range);
};

#endif
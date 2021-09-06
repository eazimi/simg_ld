#ifndef SYNC_PROC_HPP
#define SYNC_PROC_HPP

#include "channel.hpp"
#include <event2/event.h>
#include <memory>
#include <unordered_set>
#include <list>
#include <map>

using namespace std;

class Loader;

class SyncProc {
private:
  unique_ptr<event_base, decltype(&event_base_free)> base_{nullptr, &event_base_free};
  vector<unique_ptr<event, decltype(&event_free)>> socket_event_;
  unique_ptr<event, decltype(&event_free)> signal_event_{nullptr, &event_free};

  map<short, unique_ptr<Channel>> ch_hash;

public:
  explicit SyncProc() = default;

  // No copy
  SyncProc(SyncProc const&) = delete;
  SyncProc& operator=(SyncProc const&) = delete;
  SyncProc& operator=(SyncProc&&) = delete;

  void start(void (*handler)(int, short, void*), Loader *loader, list<int> sockets);
  void dispatch() const;
  void break_loop() const;

  inline const Channel& get_channel(short event) { return *(ch_hash[event].get());  }
};

#endif
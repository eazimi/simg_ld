#ifndef SYNC_PROC_MULTI_H
#define SYNC_PROC_MULTI_H

#include <event2/event.h>
#include <memory>

using namespace std;

class Loader;

class SyncProcMulti {
private:
  unique_ptr<event_base, decltype(&event_base_free)> base_{nullptr, &event_base_free};
  unique_ptr<event, decltype(&event_free)> signal_event_{nullptr, &event_free};

public:
  explicit SyncProcMulti() = default;
  void start(void (*handler)(int, short, void*), Loader *loader);

  void dispatch() const;
  void break_loop() const;
};

#endif
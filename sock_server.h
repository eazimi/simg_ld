#ifndef SOCK_SERVER_H
#define SOCK_SERVER_CPP

#include <event2/event.h>
#include <memory>

using namespace std;

class Loader;
class SockServer {
private:
  unique_ptr<event_base, decltype(&event_base_free)> base_{nullptr, &event_base_free};
  unique_ptr<event, decltype(&event_free)> socket_event_{nullptr, &event_free};
  unique_ptr<event, decltype(&event_free)> signal_event_{nullptr, &event_free};

public:
  explicit SockServer() = default;
  // No copy
  SockServer(SockServer const&) = delete;
  SockServer& operator=(SockServer const&) = delete;
  SockServer& operator=(SockServer&&) = delete;

  void start(void (*handler)(int, short, void*), Loader *loader);
  void dispatch() const;
  void break_loop() const;
};

#endif
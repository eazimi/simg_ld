#ifndef SYNC_PROC_HPP
#define SYNC_PROC_HPP

#include "channel.hpp"
#include <event2/event.h>
#include <memory>
#include <unordered_set>

class SyncProc {
private:
  unique_ptr<event_base, decltype(&event_base_free)> base_{nullptr, &event_base_free};
  unique_ptr<event, decltype(&event_free)> socket_event_{nullptr, &event_free};
  unique_ptr<event, decltype(&event_free)> signal_event_{nullptr, &event_free};

  Channel channel_;
  std::unordered_set<pid_t> procs_;

public:
  explicit SyncProc(int sockfd, std::unordered_set<pid_t>&& procs) : channel_(sockfd), procs_(std::move(procs)) {}
  // No copy
  SyncProc(SyncProc const&) = delete;
  SyncProc& operator=(SyncProc const&) = delete;
  SyncProc& operator=(SyncProc&&) = delete;

  void start(void (*handler)(int, short, void*));
  void dispatch() const;
  void break_loop() const;
  void handle_waitpid();
  void remove_process(pid_t pid);

  inline const Channel& get_channel() { return channel_; }
};

#endif
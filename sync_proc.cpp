#include "sync_proc.hpp"

#include "global.hpp"
#include <assert.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

void SyncProc::start(void (*handler)(int, short, void*), Loader *loader)
{
  // DLOG(INFO, "SyncProc: start called\n");
  auto* base = event_base_new();
  base_.reset(base);

  auto* socket_event = event_new(base, get_channel().get_socket(), EV_READ | EV_PERSIST, handler, loader);
  event_add(socket_event, nullptr);
  socket_event_.reset(socket_event);

  auto* signal_event = event_new(base, SIGCHLD, EV_SIGNAL | EV_PERSIST, handler, loader);
  event_add(signal_event, nullptr);
  signal_event_.reset(signal_event);
  dispatch();
}

void SyncProc::dispatch() const
{
  // DLOG(INFO, "SyncProc: dispatch called\n");
  event_base_dispatch(base_.get());
}

void SyncProc::break_loop() const
{
  DLOG(INFO, "SyncProc: break_loop called\n");
  event_base_loopbreak(base_.get());
}
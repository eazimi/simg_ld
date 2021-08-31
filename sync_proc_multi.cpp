#include "sync_proc_multi.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <signal.h>

void SyncProcMulti::start(void (*handler)(int, short, void*), Loader* loader)
{
  auto* base = event_base_new();
  base_.reset(base);

  auto* signal_event = event_new(base, SIGCHLD, EV_SIGNAL | EV_PERSIST, handler, loader);
  event_add(signal_event, nullptr);
  signal_event_.reset(signal_event);
  dispatch();
}

void SyncProcMulti::dispatch() const
{
  event_base_dispatch(base_.get());
}

void SyncProcMulti::break_loop() const
{
  event_base_loopbreak(base_.get());
}

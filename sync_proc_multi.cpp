#include "sync_proc_multi.h"

#include <netinet/in.h>
#include <sys/socket.h>

void SyncProcMulti::start(void (*handler)(int, short, void*), Loader* loader)
{
  auto* base = event_base_new();
  base_.reset(base);
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

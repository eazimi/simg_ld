#include "sync_proc.hpp"

#include <signal.h>

void SyncProc::start(void (*handler)(int, short, void *))
{
    auto *base = event_base_new();
    base_.reset(base);

    auto *socket_event = event_new(base, get_channel().get_socket(), EV_READ | EV_PERSIST, handler, this);
    event_add(socket_event, nullptr);
    socket_event_.reset(socket_event);

    auto *signal_event = event_new(base, SIGCHLD, EV_SIGNAL | EV_PERSIST, handler, this);
    event_add(signal_event, nullptr);
    signal_event_.reset(signal_event);
}

void SyncProc::dispatch() const
{
    event_base_dispatch(base_.get());
}

void SyncProc::break_loop() const
{
    event_base_loopbreak(base_.get());
}

#ifndef SYNC_PROC_HPP
#define SYNC_PROC_HPP

#include <event2/event.h>
#include <memory>
#include "channel.hpp"

class SyncProc
{
private:
    unique_ptr<event_base, decltype(&event_base_free)> base_{nullptr, &event_base_free};
    unique_ptr<event, decltype(&event_free)> socket_event_{nullptr, &event_free};
    unique_ptr<event, decltype(&event_free)> signal_event_{nullptr, &event_free};

    Channel channel_;

public:
    explicit SyncProc(int sockfd) : channel_(sockfd) {}
    // No copy
    SyncProc(SyncProc const &) = delete;
    SyncProc &operator=(SyncProc const &) = delete;
    SyncProc &operator=(SyncProc &&) = delete;

    void start(void (*handler)(int, short, void *));
    void dispatch() const;
    void break_loop() const;

    inline const Channel &get_channel() { return channel_; }
};

#endif
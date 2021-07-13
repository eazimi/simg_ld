#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <string>

using namespace std;

class Channel
{
private:
    int socket_ {-1};

public:
    explicit Channel(int socket) : socket_(socket) {}
    ~Channel();

    // no copy
    Channel(const Channel &) = delete;
    Channel& operator=(const Channel &) = delete;

    int send(const void *message, size_t size) const;
    size_t receive(void *message, size_t size, bool block = true) const;

    inline int getSocket() const { return socket_; }
};

#endif
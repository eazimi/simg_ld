#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include "global.hpp"
#include <string>

using namespace std;

enum class MessageType
{
    NONE,
    READY,
    CONTINUE,
    FINISH
};

/* Child->Parent */
struct s_message_t {
  MessageType type;
  pid_t pid;
};

class Channel
{
private:
    int socket_{-1};
    template<class M> static constexpr bool messageType()
    {
        return std::is_trivial<M>::value && std::is_class<M>::value;
    }

public:
    explicit Channel(int socket) : socket_(socket) {}
    ~Channel();

    // no copy
    Channel(const Channel &) = delete;
    Channel& operator=(const Channel &) = delete;

    // send
    int send(const void *message, size_t size) const;
    template <class M>
    typename std::enable_if_t<messageType<M>(), int> send(M const &m) const
    {
        return this->send(&m, sizeof(M));
    }

    // receive
    size_t receive(void *message, size_t size, bool block = true) const;
    template <class M>
    typename std::enable_if_t<messageType<M>(), ssize_t> receive(M &m) const
    {
        return this->receive(&m, sizeof(M));
    }

    inline int get_socket() const { return socket_; }
};

#endif
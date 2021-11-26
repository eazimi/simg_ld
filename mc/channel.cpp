#include "channel.hpp"
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

Channel::~Channel()
{
  if (socket_ >= 0)
    close(socket_);
}

int Channel::send(const void* message, size_t size) const
{
  while (::send(socket_, message, size, 0) == -1) {
    if (errno != EINTR) {
      cout << "Channel::send failure: " << strerror(errno) << endl;
      return errno;
    }
  }
  return 0;
}

size_t Channel::receive(void* message, size_t size, bool block) const
{
  ssize_t res = recv(socket_, message, size, block ? 0 : MSG_DONTWAIT);
  if (res == -1)
    cout << "Channel::receive failure: " << strerror(errno) << endl;
  return res;
}
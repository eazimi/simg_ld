#include "sync_proc_multi.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <signal.h>
#include <fcntl.h>
#include "global.hpp"

void SyncProcMulti::start(void (*handler)(int, short, void*), Loader* loader)
{
  auto* base = event_base_new();
  base_.reset(base);

  auto* signal_event = event_new(base, SIGCHLD, EV_SIGNAL | EV_PERSIST, handler, loader);
  event_add(signal_event, nullptr);
  signal_event_.reset(signal_event);
     
  // 1. Create a TCP socket
  int opt = 1;
  int listen_sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  ASSERTNOERR(listen_sockfd);

  auto val = setsockopt(listen_sockfd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
  ASSERTNOERR(val);

  // 2. Bind it to a port
  const int LISTEN_PORT  = 9000;
  const int LISTEN_QUEUE = 100;
  sockaddr_in srvAddr;
  bzero(&srvAddr, sizeof(struct sockaddr_in));
  srvAddr.sin_family      = AF_INET;
  srvAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  srvAddr.sin_port        = htons(LISTEN_PORT);
  bind(listen_sockfd, (sockaddr*)&srvAddr, sizeof(srvAddr));

  // 3. listen
  val = listen(listen_sockfd, LISTEN_QUEUE);
  ASSERTNOERR(val);

  // Set the socket to non-blocking
	auto flags = fcntl(listen_sockfd, F_GETFL);
  ASSERTNOERR(flags);
	flags |= O_NONBLOCK;
	val = fcntl(listen_sockfd, F_SETFL, flags);
  ASSERTNOERR(val);

  auto* socket_event = event_new(base, listen_sockfd, EV_READ | EV_PERSIST, handler, loader);
  event_add(socket_event, nullptr);

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

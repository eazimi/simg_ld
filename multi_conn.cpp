#include "multi_conn.h"

constexpr int PORT        = 9000;
constexpr int BUFFER_SIZE = 1024;

constexpr int simg_assert(int n, char* message)
{
  if (n == -1) {
    perror(message);
    exit(1);
  }
  return n;
}

Connection::~Connection() {}

Connection* Connection::create(Conn_Type conn_type)
{
  switch (conn_type) {
    case Conn_Type::client:
      return new Client();

    case Conn_Type::server:
      return new Server();

    default:
      return nullptr;
  }
}

Client::Client()
{
  init();
}

void Client::init()
{
  // fd_set rfds;
  // struct timeval tv;
  // int retval, maxfd;

  /// Define sockfd
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  /// Define sockaddr_in
  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family      = AF_INET;
  servaddr.sin_port        = htons(PORT);            /// Server Port
  servaddr.sin_addr.s_addr = inet_addr("127.0.0.1"); /// server ip

  // Connect to the server, successfully return 0, error return - 1
  simg_assert(connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)), (char*)"could not connect to server");
}

void Client::send(char* message)
{
  ::send(sockfd, message, strlen(message), 0); // Send out
}

Server::Server()
{
  init();
}

void Server::init()
{
  sockfd    = simg_assert(socket(AF_INET, SOCK_STREAM, 0), (char*)"could not create TCP listening socket");
  int flags = simg_assert(fcntl(sockfd, F_GETFL), (char*)"could not get flags on TCP listening socket");
  simg_assert(fcntl(sockfd, F_SETFL, flags | O_NONBLOCK),
              (char*)"could not set TCP listening socket to be non-blocking");

  // set master socket to allow multiple connections ,
  // this is just a good habit, it will work without this
  int opt = 1;
  simg_assert(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)),
              (char*)"could not set socket options");

  struct sockaddr_in addr;
  unsigned long addr_len = sizeof(addr);
  addr.sin_family        = AF_INET;
  addr.sin_port          = PORT;
  addr.sin_addr.s_addr   = htonl(INADDR_ANY);
  simg_assert(bind(sockfd, reinterpret_cast<sockaddr*>(&addr), addr_len), (char*)"socket could not bind");
  simg_assert(listen(sockfd, 100), (char*)"socket could not listen");

  // set of socket descriptors
  fd_set readfds;
  int max_sd;
  list<int> client_sockets;
  for (;;) {
    FD_ZERO(&readfds);

    // add master socket to set
    FD_SET(sockfd, &readfds);
    max_sd = sockfd;
    for (auto so : client_sockets) {
      if (so > 0)
        FD_SET(so, &readfds);
      max_sd = max(max_sd, so);
    }

    // wait for an activity on one of the sockets , timeout is NULL ,
    // so wait indefinitely
    auto activity = select(max_sd + 1, &readfds, nullptr, nullptr, nullptr);
    if (activity < 0 && (errno != EINTR))
      DLOG(NOISE, "select() error in server\n");

    // If something happened on the master socket ,
    // then its an incoming connection
    if (FD_ISSET(sockfd, &readfds)) {
      int new_socket = accept(sockfd, reinterpret_cast<sockaddr*>(&addr), reinterpret_cast<socklen_t*>(addr_len));
      simg_assert(new_socket, (char*)"accept() error in server");

      // inform user of socket number - used in send and receive commands
      DLOG(INFO, "New connection , socket fd is %d , ip is : %s , port : %d \n", new_socket, inet_ntoa(addr.sin_addr),
           ntohs(addr.sin_port));

      // send new connection greeting message
      char* message = (char*)"ECHO Daemon\n";
      if (send(new_socket, message, strlen(message), 0) != strlen(message)) {
        DLOG(ERROR, "send() error in server");
      }

      // add new socket to array of sockets
      client_sockets.push_back(new_socket);

      for (auto it = client_sockets.begin(); it != client_sockets.end(); it++) {
        auto so = *it;
        if (FD_ISSET(so, &readfds)) {
          // Check if it was for closing , and also read the
          // incoming message
          int val_read;
          char buffer[1025];
          if ((val_read = ::read(so, buffer, 1024)) == 0) {
            // Somebody disconnected , get his details and print
            getpeername(so, reinterpret_cast<sockaddr*>(&addr), reinterpret_cast<socklen_t*>(&addr_len));
            DLOG(INFO, "Host disconnected , ip %s , port %d \n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

            // Close the socket and mark as 0 in list for reuse
            close(so);
            client_sockets.erase(it);
          }

          // Echo back the message that came in
          else {
            // set the string terminating NULL byte on the end
            // of the data read
            buffer[val_read] = '\0';
            ::send(so, buffer, strlen(buffer), 0);
          }
        }
      }
    }
  }
}

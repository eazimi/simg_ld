#ifndef MULTI_CONN_H
#define MULTI_CONN_H

#include "global.hpp"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <list>
#include <memory>

using namespace std;

enum class Conn_Type { server, client };

class Connection {
public:
  virtual void init() = 0;
  virtual ~Connection() = 0;
  static Connection* create(Conn_Type conn_type);
};

class Server : public Connection {
public:
  explicit Server();
  ~Server()
  {
    if (sockfd > 0)
      close(sockfd);
  }

  void init();

private:
  int sockfd;
};

class Client : public Connection {
public:
  explicit Client();
  ~Client()
  {
    if (sockfd > 0)
      close(sockfd);
  }

  void init();
  void send(char* message);

private:
  int sockfd;
};

class MakeConnection
{
  public:
    explicit MakeConnection(Conn_Type conn_type) {
      connection = Connection::create(conn_type);      
    }
    inline Connection* get_connection() { return connection; }

    ~MakeConnection()
    {
      if(connection)
      {
        delete[] connection;
        connection = nullptr;
      }
    }

  private: 
     Connection* connection;
};

#endif
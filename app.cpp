#include "app.h"
#include <assert.h>
#include <csignal>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/types.h>

using namespace std;

App::App(const char* socket)
{
  reserved_area = std::make_unique<MemoryArea_t>();
  init(socket);
}

void App::release_parent_memory_region(vector<string> memlayout) const
{
  // look for /usr/lib64/
  const char* token_lib = "/usr/lib64/";
  // const char* token_mc = "/build/mc";
  const char* token_simgld = "/build/simgld";
  const char* token_space = " ";
  const char* token_dash = "-";
  auto memlayout_size = memlayout.size();
  for (auto i = 0; i < memlayout_size; i++) {
    auto line = const_cast<char*>(memlayout[i].c_str());
    auto ret_lib = strstr(line, token_lib);
    // auto ret_mc = strstr(line, token_mc);
    auto ret_simgld = strstr(line, token_simgld);
    if((ret_lib == nullptr) /*&& (ret_mc == nullptr)*/ && (ret_simgld == nullptr))
      continue;
    auto token = strtok(line, token_space);    
    auto str_begin = strtok(token, token_dash);
    auto begin_addr = strtoul(str_begin, nullptr, 16);
    auto str_end = &token[strlen(str_begin)+1];
    auto end_addr = strtoul(str_end, nullptr, 16);
    auto ret_munmap = munmap((void*)begin_addr, end_addr-begin_addr);
    if (ret_munmap != 0)
      DLOG(ERROR, "app %d: munmap %s-%s was NOT successful. err: %s\n", getpid(), str_begin, 
                  str_end, strerror(errno));
  }
}

void App::get_reserved_memory_region(std::pair<void*, void*>& range)
{
  Area area;
  bool found = false;
  int mapsfd = open("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    DLOG(ERROR, "Failed to open proc maps\n");
    return;
  }
  while (readMapsLine(mapsfd, &area)) {
    if (strstr(area.name, "[stack]")) {
      found = true;
      break;
    }
  }
  close(mapsfd);

  if (found) {
    reserved_area->start = (VA)area.addr - GB3;
    reserved_area->end   = (VA)area.addr - GB1;
    range.first          = reserved_area->start;
    range.second         = reserved_area->end;
  }
}

void App::init(const char* socket)
{
  int fd   = str_parse_int(socket, "Socket id is not in a numeric format");
  channel_ = make_unique<Channel>(fd);

  // Check the socket type/validity:
  int type;
  socklen_t socklen = sizeof(type);
  assert((getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &socklen) == 0) && "Could not check socket type");
  stringstream ss;
  ss << "Unexpected socket type " << type;
  auto str = ss.str().c_str();
  assert((type == SOCK_SEQPACKET) && str);

  // Wait for the parent:
  errno = 0;
#if defined __linux__
  ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
#elif defined BSD
  ptrace(PT_TRACE_ME, 0, nullptr, 0);
#else
#error "no ptrace equivalent coded for this platform"
#endif

  ss << "Could not wait for the parent (errno = %d: %s)" << errno << strerror(errno);
  str = ss.str().c_str();

  DLOG(NOISE, "app %d: before SIGSTOP\n", getpid());
  assert((errno == 0 && raise(SIGSTOP) == 0) && str); // Wait for the parent to awake me
  DLOG(NOISE, "app %d: PTRACE_CONT received\n", getpid());

  write_mmapped_ranges("app-completely_loaded-init()", getpid());

  s_message_t message{MessageType::LOADED, getpid()};
  assert(channel_->send(message) == 0 && "Could not send the LOADED message.");
  handle_message();
  DLOG(ERROR, "never reach this line ...\n");
}

void App::handle_message() const
{
  bool loop = true;  
  while (loop) {
    std::array<char, sizeof(s_message_t)> message_buffer;
    ssize_t received_size = channel_->receive(message_buffer.data(), message_buffer.size());
    assert(received_size >= 0 && "Could not receive commands from the parent");

    const s_message_t* message = (s_message_t*)message_buffer.data();
    switch (message->type) {
      case MessageType::CONTINUE:
        DLOG(INFO, "app %d: mc sent a %s message\n", getpid(), "CONTINUE");
        s_message_t base_message;
        base_message.type = MessageType::FINISH;
        base_message.pid  = getpid();
        channel_->send(base_message);
        break;

      case MessageType::LAYOUT: {
        auto memlayout      = message->memlayout;
        auto memlayout_size = message->memlayout_size;
        DLOG(INFO, "app %d: mc sent a %s message\n", getpid(), "LAYOUT");
        vector<string> vec_memlayout;
        // cout << "memory layout of mc:" << endl;
        for (auto i = 0; i < memlayout_size; i++)
        {
          vec_memlayout.push_back(memlayout[i]);
          // cout << memlayout[i] << endl;
        }
        
        release_parent_memory_region(vec_memlayout);
        write_mmapped_ranges("app-after_release_mc_mem-handleMessage()", getpid());
        s_message_t base_message;
        base_message.type = MessageType::READY;
        base_message.pid  = getpid();
        channel_->send(base_message);
      } break;

      case MessageType::DONE:
        DLOG(INFO, "app %d: mc sent a %s message\n", getpid(), "DONE");
        // loop = false;
        break;

      default:
        DLOG(ERROR, "app %d: mc sent an invalid message\n", getpid());
    }
  }
  // raise(SIGINT);
}
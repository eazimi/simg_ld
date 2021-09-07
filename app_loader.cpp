#include "app_loader.h"
#include <assert.h>
#include <csignal>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/types.h>

using namespace std;

AppLoader::AppLoader(const char* socket)
{
  reserved_area = std::make_unique<MemoryArea_t>();
  init(socket);
}

void AppLoader::release_parent_memory_region()
{
  const string maps_path = "/proc/self/maps";
  vector<string> tokens{"/simg_ld", "[heap]", "[stack]", "[vvar]", "[vdso]"};
  vector<pair<unsigned long, unsigned long>> all_addr = getRanges(maps_path, tokens);
  for (auto it : all_addr) {
    auto ret = munmap((void*)it.first, it.second - it.first);
    if (ret != 0)
      cout << "munmap was not successful: " << strerror(errno) << " # " << std::hex << it.first << " - " << std::hex
           << it.second << endl;
  }
}

void AppLoader::get_reserved_memory_region(std::pair<void*, void*>& range)
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
    reserved_area->start = (VA)area.addr - _3_GB;
    reserved_area->end   = (VA)area.addr - _1_GB;
    range.first          = reserved_area->start;
    range.second         = reserved_area->end;
  }
}

void AppLoader::init(const char* socket)
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

  // std::cout << "[CHILD], memory layout BEFORE unmmap ..." << std::endl;
  // print_mmapped_ranges();

  DLOG(NOISE, "app_loader: before SIGSTOP, pid: %d\n", getpid());
  assert((errno == 0 && raise(SIGSTOP) == 0) && str); // Wait for the parent to awake me
  DLOG(NOISE, "app_loader: PTRACE_CONT received, pid: %d\n", getpid());

  // do munmap
  // std::cout << "[CHILD], memory layout AFTER unmmap ..." << std::endl;
  release_parent_memory_region();
  // print_mmapped_ranges();

  s_message_t message{MessageType::READY, getpid()};
  assert(channel_->send(message) == 0 && "Could not send the initial message.");
  // DLOG(INFO, "child: app_loader sent a %s message to the parent\n", "READY");
  handle_message();
  DLOG(ERROR, "never reach this line ...\n");
}

void AppLoader::handle_message() const
{
  vector<string> str_messages{"NONE", "READY", "CONTINUE", "FINISH", "DONE"};
  bool loop = true;  
  while (loop) {
    std::array<char, MESSAGE_LENGTH> message_buffer;
    ssize_t received_size = channel_->receive(message_buffer.data(), message_buffer.size());
    assert(received_size >= 0 && "Could not receive commands from the parent");

    const s_message_t* message = (s_message_t*)message_buffer.data();
    switch (message->type) {
      case MessageType::CONTINUE:
        DLOG(INFO, "child: parent sent a %s message, pid = %d\n", "CONTINUE", getpid());
        s_message_t base_message;
        base_message.type = MessageType::FINISH;
        base_message.pid  = getpid();
        channel_->send(base_message);
        break;

      case MessageType::DONE:
        DLOG(INFO, "child: parent sent a %s message, pid = %d\n", "DONE", getpid());
        // loop = false;
        break;

      default:
        DLOG(ERROR, "child: parent sent an invalid message, \n");
    }
  }
  // raise(SIGINT);
}
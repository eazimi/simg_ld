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

AppLoader::AppLoader()
{
  reserved_area = std::make_unique<MemoryArea_t>();
  init();
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

void AppLoader::init()
{
  // Wait for the parent:
  errno = 0;
#if defined __linux__
  ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
#elif defined BSD
  ptrace(PT_TRACE_ME, 0, nullptr, 0);
#else
#error "no ptrace equivalent coded for this platform"
#endif

  stringstream ss;
  ss << "Could not wait for the parent (errno = %d: %s)" << errno << strerror(errno);
  DLOG(NOISE, "app_loader: before SIGSTOP\n");
  assert((errno == 0 && raise(SIGSTOP) == 0) && ss.str().c_str()); // Wait for the parent to awake me
  DLOG(NOISE, "app_loader: PTRACE_CONT received\n");

  handle_message();
  DLOG(ERROR, "never reach this line ...\n");
}

void AppLoader::handle_message() const
{
  vector<string> str_messages{"NONE", "READY", "CONTINUE", "FINISH", "DONE"};
  bool loop = true;  
  while (loop) {
    // std::array<char, MESSAGE_LENGTH> message_buffer;
    // ssize_t received_size = channel_->receive(message_buffer.data(), message_buffer.size());
    // assert(received_size >= 0 && "Could not receive commands from the parent");

    // const s_message_t* message = (s_message_t*)message_buffer.data();
    // switch (message->type) {
    //   case MessageType::CONTINUE:
    //     DLOG(INFO, "child %i: parent sent a %s message\n", getpid(), "CONTINUE");
    //     s_message_t base_message;
    //     base_message.type = MessageType::FINISH;
    //     base_message.pid  = getpid();
    //     channel_->send(base_message);
    //     break;

    //   case MessageType::DONE:
    //     DLOG(INFO, "child %i: parent sent a %s message\n", getpid(), "DONE");
    //     // loop = false;
    //     break;

    //   default:
    //     DLOG(ERROR, "child %i: parent sent an invalid message, \n", getpid());
    // }
  }
  // raise(SIGINT);
}
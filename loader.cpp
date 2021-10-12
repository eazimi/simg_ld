#include "loader.h"

int Loader::init(const char** argv, pair<int, int>& param_count)
{
  auto param_index = process_argv(argv, param_count);
  if (param_index == -1) {
    DLOG(ERROR, "Command line parameters are invalid\n");
    DLOG(ERROR, "Usage: ./simg_ld /PATH/TO/APP1 [APP1_PARAMS] -- /PATH/TO/APP2 [APP2_PARAMS]\n");
    DLOG(ERROR, "exiting ...\n");
    exit(-1);
  }

  _parent_pid = getpid();

  // reserve some 2 GB in the address space, lock remained free areas
  write_mmapped_ranges("before_reserve", 0);
  vm_->reserve_mem_space(GB2);
  write_mmapped_ranges("after_reserve", 0);

  return param_index;
}

void Loader::run(const char** argv)
{
  std::pair<int, int> param_count;
  auto param_index = init((const char**)argv, param_count);

  std::stringstream ss;
  ss << "[PARENT], before fork: getpid() = " << std::dec << getpid();
  pause_run(ss.str());

  char* ldname = (char*)LD_NAME;
  args->set_args((char*)LD_NAME, param_index, {get<0>(param_count), get<1>(param_count)});

  const int CHILD_COUNT = 2;
  for (auto i = 0; i < CHILD_COUNT; i++) {
    // Create an AF_LOCAL socketpair used for exchanging messages
    // between the model-checker process (ourselves) and the model-checked
    // process:
    int sockets[2];
    assert((socketpair(AF_LOCAL, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) != -1) && "Could not create socketpair");

    pid_t pid = fork();
    assert(pid >= 0 && "Could not fork child process");

    if (pid == 0) // child
    {
      ::close(sockets[1]);
#ifdef __linux__
      // Make sure we do not outlive our parent
      sigset_t mask;
      sigemptyset(&mask);
      assert(sigprocmask(SIG_SETMASK, &mask, nullptr) >= 0 && "Could not unblock signals");
      assert(prctl(PR_SET_PDEATHSIG, SIGHUP) == 0 && "Could not PR_SET_PDEATHSIG");
#endif

      int fdflags = fcntl(sockets[0], F_GETFD, 0);
      assert((fdflags != -1 && fcntl(sockets[0], F_SETFD, fdflags & ~FD_CLOEXEC) != -1) &&
             "Could not remove CLOEXEC for socket");

      appLoader_->runRtld(0, param_count.first, sockets[0]);
    } else // parent
    {
      procs_.push_back(pid);
      ::close(sockets[0]);
      sockets_.push_back(sockets[1]);
    }
  }

  write_mmapped_ranges("all_apps_runnig", 0); 

  // due to run_child_process(), child never reaches here
  sync_proc_ = make_unique<SyncProc>();
  sync_proc_->start(
      [](evutil_socket_t sig, short event, void* arg) {
        auto loader = static_cast<Loader*>(arg);
        if (event == EV_READ) {
          std::array<char, MESSAGE_LENGTH> buffer;
          ssize_t size = loader->sync_proc_->get_channel(sig).receive(buffer.data(), buffer.size(), false);
          if (size == -1 && errno != EAGAIN) {
            DLOG(ERROR, "%s\n", strerror(errno));
            exit(-1);
          }
          loader->handle_message(sig, buffer.data());
        } else if (event == EV_SIGNAL) {
          if (sig == SIGCHLD) {
            loader->handle_waitpid();
          }
        } else {
          DLOG(ERROR, "Unexpected event\n");
          exit(-1);
        }
      },
      this, sockets_);
}

void Loader::remove_process(pid_t pid)
{
  auto it = std::find(procs_.begin(), procs_.end(), pid); 
  if (it != procs_.end())
    procs_.erase(it);
}

void Loader::handle_message(int socket, void* buffer)
{
  vector<string> str_messages{"NONE", "READY", "CONTINUE", "FINISH", "DONE"};
  s_message_t base_message;
  memcpy(&base_message, static_cast<char*>(buffer), sizeof(base_message));
  auto str_message_type = str_messages[static_cast<int>(base_message.type)];
  DLOG(INFO, "parent: child %d sent a %s message, socket = %d\n", base_message.pid, str_message_type.c_str(), socket);

  base_message.pid = getpid();
  bool run_app     = false;
  if (base_message.type == MessageType::READY) {
    base_message.type = MessageType::CONTINUE;
  } else if (base_message.type == MessageType::FINISH) {
    base_message.type = MessageType::DONE;
    run_app           = true;
  }
  sync_proc_->get_channel(socket).send(base_message);
  if (run_app) { // 0: ldname, 1: param_index, 2: param_count
    // run_rtld(args->ld_name(), args->param_index(), args->param_count(1));
  }
  // if (!sync_proc->handle_message(buffer.data(), size))
  //   sync_proc->break_loop();
}

void Loader::handle_waitpid()
{
  int status;
  pid_t pid;
  while ((pid = waitpid(-1, &status, WNOHANG)) != 0) {
    if (pid == -1) {
      if (errno == ECHILD) {
        // No more children:
        assert((procs_.size() == 0) && "Inconsistent state");
        break;
      } else {
        DLOG(ERROR, "Could not wait for pid\n");
        exit(-1);
      }
    }

    auto it = find(procs_.begin(), procs_.end(), pid);
    if (it == procs_.end()) {
      DLOG(ERROR, "Child process not found\n");
      return;
    } else {
      // From PTRACE_O_TRACEEXIT:
#ifdef __linux__
      if (status >> 8 == (SIGTRAP | (PTRACE_EVENT_EXIT << 8))) {
        assert((ptrace(PTRACE_GETEVENTMSG, pid, 0, &status) != -1) && "Could not get exit status");
        if (WIFSIGNALED(status)) {
          DLOG(ERROR, "CRASH IN THE PROGRAM, %i\n", status);
          for (auto process : procs_)
            kill(process, SIGKILL);
          exit(-1);
        }
      }
#endif

      // We don't care about signals, just reinject them:
      if (WIFSTOPPED(status)) {
        // DLOG(INFO, "Stopped with signal %i\n", (int)WSTOPSIG(status));
        errno = 0;
#ifdef __linux__
        ptrace(PTRACE_CONT, pid, 0, WSTOPSIG(status));
#endif
        assert(errno == 0 && "Could not PTRACE_CONT");
      } else if (WIFSIGNALED(status)) {
        DLOG(ERROR, "CRASH IN THE PROGRAM, %i\n", status);
        for (auto process : procs_)
          kill(process, SIGKILL);
        exit(-1);
      } else if (WIFEXITED(status)) {
        DLOG(INFO, "Child process is over\n");
        procs_.erase(it);
      }
    }
  }
}

void Loader::release_reserved_memory_region()
{
  munmap(g_range_->start, (unsigned long)g_range_->end - (unsigned long)g_range_->start);
}

void Loader::hide_free_memory_regions()
{
  std::vector<MemoryArea_t> mmaps_range{};
  Area area;
  int mapsfd     = open("/proc/self/maps", O_RDONLY);
  bool firstLine = true;
  MemoryArea_t range;
  while (readMapsLine(mapsfd, &area)) {
    // todo: check if required to add this condition: (area.endAddr >= (VA)&area)
    if (firstLine) {
      range.start = area.endAddr;
      firstLine   = false;
      continue;
    }
    range.end = area.addr;
    mmaps_range.push_back(std::move(range));
    range.start = area.endAddr;
  }
  close(mapsfd);
  mmaps_range.pop_back();

  auto mmaps_size = mmaps_range.size() - 1;
  for (auto i = 0; i <= mmaps_size; i++) {
    auto start_mmap = (unsigned long)(mmaps_range[i].start);
    auto length     = (unsigned long)(mmaps_range[i].end) - start_mmap;
    if (length == 0)
      continue;
    void* mmap_ret = mmap((void*)start_mmap, length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (mmap_ret == MAP_FAILED) {
      DLOG(ERROR, "failed to lock the free spot. %s\n", strerror(errno));
      exit(-1);
    }
  }

  ofstream ofs("./log/free_space_locked.txt", ofstream::out);
  for (auto m : mmaps_range) {
    if ((unsigned long)m.end - (unsigned long)m.start > 0) {
      ofs << m.start << " - " << m.end << endl;// << " - " << end << endl;
    }
  }
  ofs.close();
}

void Loader::reserve_memory_region()
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
    g_range_->start = (VA)area.addr - GB3;
    g_range_->end   = (VA)area.addr - GB1;
  }
  // std::cout << "setReservedMemRange(): start = " << std::hex << g_range->start << " , end = " << g_range->end <<
  // std::endl;

  std::cout << "init: setReservedMemRange(): start = " << std::hex << g_range_->start << " , end = " << g_range_->end << std::endl;

  void* region = mmapWrapper(g_range_->start, (unsigned long)g_range_->end - (unsigned long)g_range_->start,
                             PROT_READ | PROT_WRITE, MAP_GROWSDOWN | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (region == MAP_FAILED) {
    DLOG(ERROR, "Failed to mmap region: %s\n", strerror(errno));
  }
}
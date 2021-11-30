#include <algorithm>
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <asm/prctl.h> /* Definition of ARCH_* constants */
#include <sys/syscall.h> /* Definition of SYS_* constants */

#include "mc.h"
#include "global.hpp"
#include "trampoline_wrappers.hpp"

MC::MC()
{
  appLoader_     = make_unique<AppLoader>();
  cmdLineParams_ = make_unique<cmdLineParams>();
  syncProc_      = make_unique<SyncProc>();
}

void MC::run(char** argv)
{
  // if (syscall(SYS_arch_prctl, ARCH_GET_FS, &lhFsAddr) < 0) {
  //   DLOG(ERROR, "Could not retrieve lower half's fs. Error: %s. Exiting...\n", strerror(errno));
  //   return;
  // }

  write_mmapped_ranges("mc-before_runRtld()-run()", getpid());
  setMemoryLayout();

  auto param_index = cmdLineParams_->process_argv(argv);
  if (param_index == -1) {
    DLOG(ERROR, "Command line parameters are invalid\n");
    DLOG(ERROR, "Usage: ./simg_ld /PATH/TO/APP1 [APP1_PARAMS] -- /PATH/TO/APP2 [APP2_PARAMS]\n");
    DLOG(ERROR, "exiting ...\n");
    exit(-1);
  }

  auto upperHalfAddr = (unsigned long)appLoader_->getStackAddr() - MB5500;
  unsigned long appAddr = atol((char*)upperHalfAddr);
  cout << "mc.cpp->run(), appAddr: 0x" << std::hex << appAddr << endl;

  auto appCount = cmdLineParams_->getAppCount();
  // todo: delete the following line
  appCount = 1;
  for (auto i = 0; i < appCount; i++) {
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

      // todo: 0 must be replaced with proper value
      auto appParams = cmdLineParams_->getAppParams(0);
      // setenv("LD_PRELOAD", "/home/eazimi/projects/simgld/build/libwrapper.so", 1);
      appLoader_->runRtld((void*)appAddr, appParams, sockets[0]);
      // while(true);
    } else // parent
    {
      allApps.push_back(pid);
      ::close(sockets[0]);
      allSockets.push_back(sockets[1]);
    }
  }

  // due to run_child_process(), child never reaches here
  syncProc_ = make_unique<SyncProc>();
  syncProc_->start(
      [](evutil_socket_t sig, short event, void* obj) {
        auto mc = static_cast<MC*>(obj);
        if (event == EV_READ) {
          std::array<char, MESSAGE_LENGTH> buffer;
          ssize_t size = mc->syncProc_->get_channel(sig).receive(buffer.data(), buffer.size(), false);
          if (size == -1 && errno != EAGAIN) {
            DLOG(ERROR, "%s\n", strerror(errno));
            exit(-1);
          }
          mc->handle_message(sig, buffer.data());
        } else if (event == EV_SIGNAL) {
          if (sig == SIGCHLD) {
            mc->handle_waitpid();
          }
        } else {
          DLOG(ERROR, "Unexpected event\n");
          exit(-1);
        }
      },
      this, allSockets);
}

void MC::handle_message(int socket, void* buffer)
{
  vector<string> str_messages{"NONE", "LOADED", "READY", "CONTINUE", "FINISH", "DONE"};

  auto message_type = ((s_message_t*)(buffer))->type;
  auto str_message_type = str_messages[static_cast<int>(((s_message_t*)(buffer))->type)];
  auto app_pid = ((s_message_t*)(buffer))->pid;

  DLOG(INFO, "mc %d: app %d sent a %s message, socket = %d\n", getpid(), app_pid, 
            str_message_type.c_str(), socket);

  s_message_t base_message;
  base_message.pid = getpid();
  base_message.type = MessageType::NONE;
  if (message_type == MessageType::LOADED) {
    auto index = 0;
    for(auto str:initialMemLayout)
    {
      auto src = str.c_str();
      memcpy(base_message.memlayout[index], src, strlen(src)+1); // copy the null-charachter too
      ++index;
    }
    // base_message.memlayout = memlayout;
    base_message.memlayout_size = index;
    base_message.type           = MessageType::LAYOUT;
  } else if (message_type == MessageType::READY) {
    base_message.type = MessageType::CONTINUE;    
  } else if (message_type == MessageType::FINISH) {
    base_message.type = MessageType::DONE;
  }
  syncProc_->get_channel(socket).send(base_message);

  // if (!sync_proc->handle_message(buffer.data(), size))
  //   sync_proc->break_loop();
}

void MC::setMemoryLayout()
{
  vector<string> memlayout;
  string mapsPath = "/proc/self/maps";
  filebuf fb;
  string line;
  if(fb.open(mapsPath, ios_base::in))
  {
    istream is(&fb);
    while(getline(is, line))
      memlayout.push_back(line);
    fb.close();
  }
  initialMemLayout.clear();
  initialMemLayout = memlayout;
}

void MC::handle_waitpid()
{
  int status;
  pid_t pid;
  while ((pid = waitpid(-1, &status, WNOHANG)) != 0) {
    if (pid == -1) {
      if (errno == ECHILD) {
        // No more children:
        assert((allApps.size() == 0) && "Inconsistent state");
        break;
      } else {
        DLOG(ERROR, "Could not wait for pid\n");
        exit(-1);
      }
    }

    auto it = find(allApps.begin(), allApps.end(), pid);
    if (it == allApps.end()) {
      DLOG(ERROR, "Child process not found\n");
      return;
    } else {
      // From PTRACE_O_TRACEEXIT:
#ifdef __linux__
      if (status >> 8 == (SIGTRAP | (PTRACE_EVENT_EXIT << 8))) {
        assert((ptrace(PTRACE_GETEVENTMSG, pid, 0, &status) != -1) && "Could not get exit status");
        if (WIFSIGNALED(status)) {
          DLOG(ERROR, "CRASH IN THE PROGRAM, %i\n", status);
          for (auto p : allApps)
            kill(p, SIGKILL);
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
        for (auto process : allApps)
          kill(process, SIGKILL);
        exit(-1);
      } else if (WIFEXITED(status)) {
        DLOG(INFO, "Child process is over\n");
        allApps.erase(it);
      }
    }
  }
}
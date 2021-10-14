#include "parent_proc.h"
#include "global.hpp"
#include <algorithm>
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

ParentProc::ParentProc()
{
  appLoader_     = make_unique<AppLoader>();
  cmdLineParams_ = make_unique<cmdLineParams>();
  syncProc_      = make_unique<SyncProc>();
}

void ParentProc::run(char** argv)
{
  auto param_index = cmdLineParams_->process_argv(argv);
  if (param_index == -1) {
    DLOG(ERROR, "Command line parameters are invalid\n");
    DLOG(ERROR, "Usage: ./simg_ld /PATH/TO/APP1 [APP1_PARAMS] -- /PATH/TO/APP2 [APP2_PARAMS]\n");
    DLOG(ERROR, "exiting ...\n");
    exit(-1);
  }
  
  // reserve some 2 GB in the address space, lock remained free areas
  // write_mmapped_ranges("before_reserve", 0);
  // appLoader_->reserveMemSpace(GB2);
  // write_mmapped_ranges("after_reserve", 0);

  auto appCount = cmdLineParams_->getAppCount();
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
      auto paramsCount = cmdLineParams_->getAppParamsCount(i);

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
      appLoader_->runRtld(appParams, sockets[0]);
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
        auto mc = static_cast<ParentProc*>(obj);
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

void ParentProc::handle_message(int socket, void* buffer)
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
  syncProc_->get_channel(socket).send(base_message);
  if (run_app) { // 0: ldname, 1: param_index, 2: param_count
    // run_rtld(args->ld_name(), args->param_index(), args->param_count(1));
  }
  // if (!sync_proc->handle_message(buffer.data(), size))
  //   sync_proc->break_loop();
}

void ParentProc::handle_waitpid()
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
#include "sync_proc.hpp"

#include <signal.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <assert.h>
#include "global.hpp"

void SyncProc::start(void (*handler)(int, short, void *))
{
    auto *base = event_base_new();
    base_.reset(base);

    auto *socket_event = event_new(base, get_channel().get_socket(), EV_READ | EV_PERSIST, handler, this);
    event_add(socket_event, nullptr);
    socket_event_.reset(socket_event);

    auto *signal_event = event_new(base, SIGCHLD, EV_SIGNAL | EV_PERSIST, handler, this);
    event_add(signal_event, nullptr);
    signal_event_.reset(signal_event);
}

void SyncProc::dispatch() const
{
    event_base_dispatch(base_.get());
}

void SyncProc::break_loop() const
{
    event_base_loopbreak(base_.get());
}

void SyncProc::handle_waitpid()
{
  DLOG(INFO, "Check for wait event\n");
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

    unordered_set<pid_t>::iterator it = procs_.find(pid);
    if(it == procs_.end())
    {
        DLOG(ERROR, "Child process not found\n");
        return;
    }
    else {
      // From PTRACE_O_TRACEEXIT:
#ifdef __linux__
      if (status>>8 == (SIGTRAP | (PTRACE_EVENT_EXIT<<8))) {
        assert((ptrace(PTRACE_GETEVENTMSG, pid, 0, &status) != -1) && "Could not get exit status");
        if (WIFSIGNALED(status)) {
            DLOG(ERROR, "CRASH IN THE PROGRAM, %i\n", status);
            for(auto process:procs_)
                kill(process, SIGKILL);
            exit(-1);
        }
      }
#endif

      // We don't care about signals, just reinject them:
      if (WIFSTOPPED(status)) {
        DLOG(INFO, "Stopped with signal %i", (int) WSTOPSIG(status));
        errno = 0;
#ifdef __linux__
        ptrace(PTRACE_CONT, pid, 0, WSTOPSIG(status));
#endif
        assert(errno == 0 && "Could not PTRACE_CONT");
      }
      else if (WIFSIGNALED(status)) {
        DLOG(ERROR, "CRASH IN THE PROGRAM, %i\n", status);
        for (auto process : procs_)
          kill(process, SIGKILL);
        exit(-1);
      }
      else if (WIFEXITED(status))
      {
        DLOG(INFO, "Child process is over\n");
        procs_.erase(it);
      }
    }
  }
}

void SyncProc::remove_process(pid_t pid)
{
    auto it = procs_.find(pid);
    if(it != procs_.end())
        procs_.erase(it);    
}

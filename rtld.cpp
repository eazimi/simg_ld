#include "rtld.h"
#include <assert.h>
#include <sys/socket.h>
#include <signal.h>
#include "global.hpp"

RTLD::RTLD()
{
  vm_ = make_unique<user_space>();
  ld_ = make_unique<LD>();
  cmdline_params_ = make_unique<cmdline_params>();
  sync_proc_ = make_unique<SyncProc>(); 
}

// template <typename T> 
// void RTLD::run_rtld(int param_index, int param_count, T p)
// {

// }

void RTLD::runAll(char** argv)
{
  pair<int, int> param_count;
  auto param_index = cmdline_params_->process_argv(argv, param_count);
  if (param_index == -1) {
    DLOG(ERROR, "Command line parameters are invalid\n");
    DLOG(ERROR, "Usage: ./simg_ld /PATH/TO/APP1 [APP1_PARAMS] -- /PATH/TO/APP2 [APP2_PARAMS]\n");
    DLOG(ERROR, "exiting ...\n");
    exit(-1);
  }

  auto childCount = cmdline_params_->getAppCount();
  for (auto i = 0; i < childCount; i++) {
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
      // run_child_process(sockets[0], [&]() { run_rtld(ldname, 0, param_count.first, sockets[0]); });
    } else // parent
    {
      // procs_.push_back(pid);
      ::close(sockets[0]);
      // sockets_.push_back(sockets[1]);
    }
  }

  // due to run_child_process(), child never reaches here
  sync_proc_ = make_unique<SyncProc>();
  sync_proc_->start(
      [](evutil_socket_t sig, short event, void* arg) {
        // auto rtld = static_cast<RTLD*>(arg);
        if (event == EV_READ) {
          cout << "parent: EV_READ" << endl;
          // std::array<char, MESSAGE_LENGTH> buffer;
          // ssize_t size = rtld->sync_proc_->get_channel(sig).receive(buffer.data(), buffer.size(), false);
          // if (size == -1 && errno != EAGAIN) {
          //   DLOG(ERROR, "%s\n", strerror(errno));
          //   exit(-1);
          // }
          // rtld->handle_message(sig, buffer.data());
        } else if (event == EV_SIGNAL) {
          if (sig == SIGCHLD) {
            cout << "parent: EV_SIGNAL" << endl;
            // rtld->handle_waitpid();
          }
        } else {
          DLOG(ERROR, "Unexpected event\n");
          exit(-1);
        }
      },
      nullptr, {});  
  
}
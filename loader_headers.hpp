#ifndef LOADER_HEADERS_HPP
#define LOADER_HEADERS_HPP

#include "dyn_obj_info.hpp"
#include "global.hpp"
#include "limits.h"
#include "sock_server.h"
#include "cmd_args.hpp"
#include <asm/prctl.h>
#include <assert.h>
#include <cstdint>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <link.h>
#include <memory>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

#endif
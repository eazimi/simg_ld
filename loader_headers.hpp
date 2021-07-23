#ifndef LOADER_HEADERS_HPP
#define LOADER_HEADERS_HPP

#include <cstdint>
#include <link.h>
#include <fcntl.h>
#include <elf.h>
#include <stdio.h>
#include <vector>
#include <memory>
#include <assert.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <asm/prctl.h>
#include <syscall.h>
#include <fstream>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <csignal>
#include <sstream>
#include <functional>
#include "limits.h"
#include "sync_proc.hpp"
#include "global.hpp"
#include "dyn_obj_info.hpp"

#endif
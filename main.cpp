#include "app_loader.h"

using namespace std;

int main(int argc, char** argv, char** env)
{
  unique_ptr<AppLoader> appLoader = make_unique<AppLoader>();
  auto mcAddr              = appLoader->reserveMemSpace(MB5500, GB2);
  auto appAddr                   = appLoader->reserveMemSpace(GB3, GB2);
  if (appAddr == MAP_FAILED || mcAddr == MAP_FAILED) {
    DLOG(ERROR, "main.cpp->main(), MAP_FAILED\n");
    return -1;
  }
  cout << "main.cpp->main(), mcAddr: " << std::hex << mcAddr << endl;
  cout << "main.cpp->main(), appAddr: " << std::hex << appAddr << endl;  
  int ret = appLoader->releaseMemSpace(appAddr, GB2);
  if (ret < 0) {
    DLOG(ERROR, "main.cpp->main()-appAddr release, %s\n", strerror(errno));
    return -1;
  }
  ret = appLoader->releaseMemSpace(mcAddr, GB2);
  if (ret < 0) {
    DLOG(ERROR, "main.cpp->main()-lowerHaldAddr release, %s\n", strerror(errno));
    return -1;
  }
  write_mmapped_ranges("simgld-after_releaseMemSpace_main()", getpid());

  appLoader->runRtld(mcAddr, appAddr);
  return 0;
}

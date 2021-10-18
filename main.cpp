#include "app_loader.h"

using namespace std;

int main(int argc, char** argv, char** env)
{
  unique_ptr<AppLoader> appLoader = make_unique<AppLoader>();
  auto upperHalfAddr = appLoader->reserveMemSpace(MB5500, GB2);
  auto lowerHalfAddr = appLoader->reserveMemSpace(GB3, GB2);
  if(upperHalfAddr == MAP_FAILED || lowerHalfAddr == MAP_FAILED) {
    DLOG(ERROR, "main.cpp->main(), MAP_FAILED\n");
    return -1;
  }
  cout << "main.cpp->main(), upperHalf_addr: " << std::hex << upperHalfAddr << endl;
  cout << "main.cpp->main(), lowerHalf_addr: " << std::hex << lowerHalfAddr << endl;
  int ret = appLoader->releaseMemSpace(upperHalfAddr, GB2);
  if(ret < 0) {
    DLOG(ERROR, "main.cpp->main()-upperHalfAddr release, %s\n", strerror(errno));
    return -1;
  }
  ret = appLoader->releaseMemSpace(lowerHalfAddr, GB2);
  if(ret < 0) {
    DLOG(ERROR, "main.cpp->main()-lowerHaldAddr release, %s\n", strerror(errno));
    return -1;
  }
  write_mmapped_ranges("simgld-after_releaseMemSpace_main()", getpid());
  appLoader->runRtld(upperHalfAddr, lowerHalfAddr);
  return 0;
}

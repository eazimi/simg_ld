#include "app_loader.h"

using namespace std;

int main(int argc, char** argv, char** env)
{
  unique_ptr<AppLoader> appLoader = make_unique<AppLoader>();
  auto upperHalf_addr = appLoader->reserveMemSpace(MB5500, GB2); 
  auto lowerHalf_addr = appLoader->reserveMemSpace(GB3, GB2);
  appLoader->setUpperHalfAddr(upperHalf_addr);
  appLoader->setLowerHalfAddr(lowerHalf_addr);
  cout << "main.cpp - upperHalf_addr: " << std::hex << appLoader->getUpperHalfAddr() << endl;
  cout << "main.cpp - lowerHalf_addr: " << std::hex << appLoader->getLowerHalfAddr() << endl;
  write_mmapped_ranges("main.cpp-[main() of simgld]", getpid());
  appLoader->runRtld(appLoader->getUpperHalfAddr(), appLoader->getLowerHalfAddr());
  return 0;
}

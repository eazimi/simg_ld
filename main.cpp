#include "app_loader.h"

using namespace std;

int main(int argc, char** argv, char** env)
{
  unique_ptr<AppLoader> appLoader = make_unique<AppLoader>();
  appLoader->runRtld();
  return 0;
}

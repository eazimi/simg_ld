#include "mc.h"

int main(int argc, char** argv, char** env)
{
  std::unique_ptr<MC> mc = std::make_unique<MC>();
  mc->run(argv);
  return 0;
}
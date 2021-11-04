#include "mc.h"

int main(int argc, char** argv, char** env)
{
  write_mmapped_ranges("mc-main()_before_run()", getpid());
  std::unique_ptr<MC> mc = std::make_unique<MC>();
  mc->run(argv);
  return 0;
}
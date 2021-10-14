#include "parent_proc.h"

int main(int argc, char** argv, char** env)
{
  std::unique_ptr<ParentProc> mc = std::make_unique<ParentProc>();
  mc->run(argv);
  return 0;
}
#include "mc.h"

int main(int argc, char** argv, char** env)
{
  // filebuf fb;
  // std::string path = "./mc_main_env.txt";
  // if (fb.open(path, std::ios_base::out)) {
  //   std::ostream os(&fb);
  //   auto p = env;
  //   while (*p++ != nullptr)
  //     os << *p << endl;
  //   fb.close();
  // }

  std::unique_ptr<MC> mc = std::make_unique<MC>();
  mc->run(argv);
  return 0;
}
#include "parent_proc.h"

int main(int argc, char** argv, char** env)
{
  // cout << "main in parent_proc_main.cpp" << endl;
  std::unique_ptr<ParentProc> mc = std::make_unique<ParentProc>();
  // cout << "mc_main.cpp, argc: " << argc << endl;
  // cout << "mc_main.cpp, argv: " << endl;
  // for(auto i=0; i<argc; i++)
  //   cout << argv[i] << endl;
  // while(true);
  mc->run(argv);
  return 0;
}
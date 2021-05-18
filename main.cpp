#include <stdio.h>
#include <iostream>
#include "ulexec.h"
#include "loader.h"

int main(int argc, char **argv, char** env)
{
    CUlexec culexec;
    // culexec.ulexec(argc, argv, env);
    (new Loader)->runRtld(argc, argv);
    std::cout << "hello world!" << std::endl;
    return 0;
}
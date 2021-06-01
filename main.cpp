#include <stdio.h>
#include <iostream>
#include "loader.h"

int main(int argc, char **argv, char** env)
{
    (new Loader)->runRtld(argc, argv);
    std::cout << "hello world!" << std::endl;
    return 0;
}
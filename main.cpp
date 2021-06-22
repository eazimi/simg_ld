#include <stdio.h>
#include <iostream>
#include "loader.h"

int main(int argc, char **argv, char** env)
{   
    std::unique_ptr<Loader> loader = std::make_unique<Loader>();
    loader->init(argc);
    loader->runRtld();
    std::cout << "hello world!" << std::endl;
    return 0;
}
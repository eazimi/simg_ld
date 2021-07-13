#include <stdio.h>
#include <iostream>
#include "loader.h"

int main(int argc, char **argv, char** env)
{   
    std::unique_ptr<Loader> loader = std::make_unique<Loader>();
    loader->run((const char **)argv);
    return 0;
}
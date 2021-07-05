#include <stdio.h>
#include <iostream>
#include "loader.h"

int main(int argc, char **argv, char** env)
{   
    std::unique_ptr<Loader> loader = std::make_unique<Loader>();
    std::pair<int, int> param_count; 
    auto param_index = loader->init((const char **)argv, param_count);
    loader->run(param_index, param_count);
    return 0;
}
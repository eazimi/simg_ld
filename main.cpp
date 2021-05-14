#include <stdio.h>
#include <iostream>
#include "ulexec.h"

int main(int argc, char **argv, char** env)
{
    CUlexec culexec;
    culexec.ulexec(argc, argv, env);
    return 0;
}
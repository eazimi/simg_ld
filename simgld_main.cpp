#include <iostream>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char** argv, char** env)
{
  execvp("./dynamic_load", &argv[0]);
  return 0;
}
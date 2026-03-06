#include "ClientCLI.h"
#include <iostream>

int main(int argc, char *argv[]) {
  try {
    ClientCLI cli;
    cli.run();
  } catch (const std::exception &e) {
    std::cerr << "Fatal Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}

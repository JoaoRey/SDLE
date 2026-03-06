#include "AdminConsole.h"

#include <csignal>
#include <getopt.h>
#include <iostream>
#include <memory>

std::unique_ptr<AdminConsole> g_console = nullptr;

void signal_handler(int signum) {
  if (signum == SIGINT) {
    std::cout << std::endl;
    Logger::getInstance().ok("AdminConsole", "Caught SIGINT. Exiting...");
    g_console.reset();
    exit(0);
  }
}

int main(int argc, char *argv[]) {
  string router_endpoint = "tcp://127.0.0.1:5555";
  bool is_main = false;

  static struct option long_options[] = {{"main", no_argument, 0, 'm'},
                                         {"router", required_argument, 0, 'r'},
                                         {0, 0, 0, 0}};

  int opt;
  int option_index = 0;
  while ((opt = getopt_long(argc, argv, "mr:", long_options, &option_index)) !=
         -1) {
    switch (opt) {
    case 'm':
      is_main = true;
      break;
    case 'r':
      router_endpoint = optarg;
      break;
    }
  }
  signal(SIGINT, signal_handler);

  // Allocate on heap
  g_console = std::make_unique<AdminConsole>(router_endpoint, is_main);
  g_console->start();
  g_console->run();
  g_console.reset();

  exit(0);
}

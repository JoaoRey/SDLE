#include "RouterServer.h"
#include "common/Logger.h"
#include <csignal>
#include <getopt.h>

using namespace std;

// Global for signal handling
RouterServer *g_server = nullptr;

void signalHandler(int signum) {
  Logger::getInstance().info("RouterServer", "Received signal " +
                                                 to_string(signum) +
                                                 ", shutting down...");
  if (g_server) {
    g_server->stop();
  }
}

int main(int argc, char *argv[]) {
  // Default config path
  string config_path;

  static struct option long_options[] = {{"config", required_argument, 0, 'c'},
                                         {0, 0, 0, 0}};

  int opt;
  int option_index = 0;
  while ((opt = getopt_long(argc, argv, "c:", long_options, &option_index)) !=
         -1) {
    switch (opt) {
    case 'c':
      config_path = optarg;
      break;
    }
  }

  // Register signal handlers
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  if (config_path.empty()) {
    Logger::getInstance().error("RouterServer",
                                "Usage: router_server --config <path>");
    return 1;
  }

  RouterServer server;
  g_server = &server;

  if (!server.initialize(config_path)) {
    Logger::getInstance().error("RouterServer",
                                "Failed to initialize router server");
    return 1;
  }

  server.run();

  return 0;
}
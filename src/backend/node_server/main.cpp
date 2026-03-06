#include "NodeServer.h"
#include "common/Logger.h"
#include <csignal>
#include <getopt.h>
#include <memory>
#include <string>

using namespace std;

// Global pointer for signal handling
unique_ptr<NodeServer> g_server;

void signalHandler(int signum) {
  Logger::getInstance().info("NodeServer", "Received signal " +
                                               to_string(signum) +
                                               ", shutting down...");
  if (g_server) {
    g_server->stop();
  }
}

int main(int argc, char *argv[]) {
  // Set up signal handling
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  g_server = make_unique<NodeServer>();

  string config_path;
  int port_override = -1;
  string router_override = "";

  static struct option long_options[] = {{"config", required_argument, 0, 'c'},
                                         {"port", required_argument, 0, 'p'},
                                         {"router", required_argument, 0, 'r'},
                                         {0, 0, 0, 0}};

  int opt;
  int option_index = 0;
  while ((opt = getopt_long(argc, argv, "c:p:r:", long_options,
                            &option_index)) != -1) {
    switch (opt) {
    case 'c':
      config_path = optarg;
      break;
    case 'p':
      port_override = stoi(optarg);
      break;
    case 'r':
      router_override = optarg;
      break;
    }
  }

  if (config_path.empty()) {
    Logger::getInstance().error("NodeServer",
                                "Usage: node_server --config <path>");
    return 1;
  }

  if (!g_server->initialize(config_path, port_override, router_override)) {
    Logger::getInstance().error("NodeServer",
                                "Failed to initialize node server");
    return 1;
  }

  Logger::getInstance().info("NodeServer", "Node " + g_server->getNodeId() +
                                               " initialized at " +
                                               g_server->getEndpoint());

  g_server->run();

  return 0;
}

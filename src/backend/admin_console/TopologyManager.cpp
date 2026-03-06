#include "TopologyManager.h"
#include "../common/Logger.h"
#include <arpa/inet.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace std;

TopologyManager::TopologyManager(ProcessManager &pm) : process_manager_(pm) {}

void TopologyManager::loadTopology(const string &config_path) {
  ifstream file(config_path);
  if (!file.is_open()) {
    Logger::getInstance().error("TopologyManager",
                                "Failed to open config: " + config_path);
    return;
  }

  nlohmann::json config;
  try {
    file >> config;
    desired_services_.clear();

    for (const auto &item : config["services"]) {
      ServiceConfig service;
      service.type = item["type"];
      service.port = item["port"];
      service.endpoint = item["endpoint"];
      if (item.contains("config_file")) {
        service.config_file = item["config_file"];
      }
      desired_services_.push_back(service);
    }

    Logger::getInstance().info("TopologyManager",
                               "Loaded topology with " +
                                   to_string(desired_services_.size()) +
                                   " services.");
  } catch (const exception &e) {
    Logger::getInstance().error(
        "TopologyManager", "Failed to parse topology: " + string(e.what()));
  }
}

void TopologyManager::ensureTopology() {
#ifdef NDEBUG
  Logger::getInstance().setState(LoggerState::SUPPRESS);
#endif

  // Check Router first
  bool router_up = false;
  string router_endpoint;

  for (const auto &service : desired_services_) {
    if (service.type == "Router") {
      router_endpoint = service.endpoint;
      if (!isServiceUp(service.endpoint)) {
        Logger::getInstance().warn("TopologyManager", "Router down at " +
                                                          service.endpoint +
                                                          ". Restarting...");
        startService(service);
      } else {
        router_up = true;
      }
    }
  }

  // TODO: Wait for router if we just started it?

  for (const auto &service : desired_services_) {
    if (service.type == "Node") {
      if (!isServiceUp(service.endpoint)) {
        Logger::getInstance().warn("TopologyManager", "Node down at " +
                                                          service.endpoint +
                                                          ". Restarting...");
        startService(service);
      }
    }
  }

#ifdef NDEBUG
  Logger::getInstance().setState(LoggerState::MINIMAL);
#endif
}

bool TopologyManager::isServiceUp(const string &endpoint) {
  // Simple TCP connect check
  // Endpoint format: tcp://127.0.0.1:port
  // Parse port
  size_t last_colon = endpoint.find_last_of(':');
  if (last_colon == string::npos)
    return false;

  int port = stoi(endpoint.substr(last_colon + 1));
  string ip = "127.0.0.1"; // Assuming localhost for now

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    return false;

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

  // Set non-blocking or timeout?
  // Standard connect might block for a while if host is down, but localhost is
  // fast.
  bool connected = false;
  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
    connected = true;
    close(sock); // Just checking connectivity
  }

  return connected;
}

void TopologyManager::startService(const ServiceConfig &service) {
  if (service.type == "Router") {
    process_manager_.startRouter(service.config_file, service.port);
  } else if (service.type == "Node") {
    // Find router endpoint
    string router_endpoint;
    for (const auto &s : desired_services_) {
      if (s.type == "Router") {
        router_endpoint = s.endpoint;
        break;
      }
    }
    process_manager_.startNode(service.config_file, service.port,
                               router_endpoint);
  }

  // Give it a moment to bind
  this_thread::sleep_for(chrono::milliseconds(20));
}

#ifndef TOPOLOGY_MANAGER_H
#define TOPOLOGY_MANAGER_H

#include "ProcessManager.h"
#include <string>
#include <vector>

using namespace std;

struct ServiceConfig {
  string type; // "Router" or "Node"
  int port;
  string endpoint;
  string config_file;
  // TODO: for nodes, we might track which router they should connect to,
  // currently hardcoded/derived
};

class TopologyManager {
public:
  TopologyManager(ProcessManager &pm);

  void loadTopology(const string &config_path);

  // Checks system state and restarts missing services
  void ensureTopology();

  string getRouterEndpoint() const {
    for (const auto &s : desired_services_) {
      if (s.type == "Router")
        return s.endpoint;
    }
    return "";
  }

private:
  ProcessManager &process_manager_;
  vector<ServiceConfig> desired_services_;

  bool isServiceUp(const string &endpoint);
  void startService(const ServiceConfig &service);
};

#endif // TOPOLOGY_MANAGER_H

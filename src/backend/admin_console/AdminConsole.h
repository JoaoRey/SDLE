#ifndef ADMIN_CONSOLE_H
#define ADMIN_CONSOLE_H

#include "../common/Logger.h"
#include "ProcessManager.h"
#include "RouterClient.h"
#include "TopologyManager.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

using namespace std;

class AdminConsole {
public:
  AdminConsole(const string &default_router_endpoint = "tcp://127.0.0.1:5555",
               bool is_main = false)
      : context_(1), router_client_(context_),
        topology_manager_(process_manager_),
        default_endpoint_(default_router_endpoint), is_main_(is_main) {
    Logger::getInstance().setState(LoggerState::MINIMAL);
    Logger::getInstance().setLevel(5);
  }

  ~AdminConsole() {
    running_ = false;

    if (is_main_) {
      if (topology_thread_.joinable()) {
        topology_thread_.join();
      }

      if (!process_manager_.getRunningProcesses().empty()) {
        Logger::getInstance().info(
            "AdminConsole", "Main console exiting. Stopping all services...");
        process_manager_.stopAllProcesses();
      }
    }
  }

  void start() {
    // Load topology first to get correct endpoints
    topology_manager_.loadTopology("config/topology.json");

    // Update default endpoint from topology if available
    string topology_router = topology_manager_.getRouterEndpoint();
    if (!topology_router.empty()) {
      default_endpoint_ = topology_router;
    }

    if (is_main_) {
      topology_manager_.ensureTopology();

      // Start background monitoring
      topology_thread_ = std::thread(&AdminConsole::topologyLoop, this);
    }

    router_client_.connect(default_endpoint_);

    // Try to ping
    if (!router_client_.ping()) {
      if (is_main_) {
        // Wait for router to start (if it was just started by ensureTopology)
        this_thread::sleep_for(chrono::seconds(1));

        auto connected = router_client_.connect(default_endpoint_);

        if (connected) {
          if (router_client_.ping()) {
            Logger::getInstance().ok("AdminConsole",
                                     "Connected to new router at " +
                                         default_endpoint_);
          } else {
            Logger::getInstance().error("AdminConsole",
                                        "Failed to ping new router.");
          }
        }
      } else {
        Logger::getInstance().warn("AdminConsole",
                                   "Router is offline and not in Main mode. "
                                   "Cannot auto-start services.");
      }
    } else {
      Logger::getInstance().ok("AdminConsole",
                               "Connected to existing router at " +
                                   default_endpoint_);
    }
  }

  void run() {
    Logger::getInstance().info(
        "AdminConsole", "Admin Console Started. Type 'help' for commands.");
    string line;

    while (running_) {
      cout << "> ";
      if (!getline(cin, line))
        break;

      if (line.empty())
        continue;

      process_manager_.checkRunningProcesses();
      processCommand(line);
    }
  }

private:
  zmq::context_t context_;
  ProcessManager process_manager_;
  RouterClient router_client_;
  TopologyManager topology_manager_;
  string default_endpoint_;
  bool is_main_;
  thread topology_thread_;
  atomic<bool> running_{true};

  void topologyLoop() {
    Logger::getInstance().info("AdminConsole", "Topology monitor started.");
    while (running_) {
      for (int i = 0; i < 50; ++i) {
        if (!running_)
          break;
        this_thread::sleep_for(chrono::milliseconds(100));
      }
      if (!running_)
        break;

      topology_manager_.ensureTopology();
    }
  }

  void processCommand(const string &line) {
    stringstream ss(line);
    string cmd;
    ss >> cmd;

    if (cmd == "help") {
      printHelp();
    } else if (cmd == "exit") {
      Logger::getInstance().ok("AdminConsole", "Exiting...");
      running_ = false;
    } else if (cmd == "list") {
      string target;
      ss >> target;
      if (target == "nodes") {
        listNodes();
      } else if (target == "routers") {
        // TODO: Implement specific function for listing routers
        process_manager_.listProcesses();
      } else if (target == "pids") {
        process_manager_.listProcesses();
      } else {
        Logger::getInstance().warn("AdminConsole",
                                   "Usage: list [nodes|routers|pids]");
      }
    } else if (cmd == "start") {
      if (!is_main_) {
        Logger::getInstance().warn(
            "AdminConsole", "Only the main console can start resources.");
      } else {
        string target;
        ss >> target;
        if (target == "router") {
          process_manager_.startRouter("config/router_config.json", 5555);
        } else if (target == "node") {
          int port;
          if (ss >> port) {
            process_manager_.startNode("config/node_config.json", port,
                                       default_endpoint_);
          } else {
            Logger::getInstance().warn("AdminConsole",
                                       "Usage: start node <port>");
          }
        } else {
          Logger::getInstance().warn("AdminConsole",
                                     "Usage: start [router|node]");
        }
      }
    } else if (cmd == "stop") {
      string arg;
      if (ss >> arg) {
        if (arg == "pids") {
          string sub_arg;
          if (ss >> sub_arg && sub_arg == "all") {
            process_manager_.stopAllProcesses();
          } else {
            Logger::getInstance().warn("AdminConsole", "Usage: stop pids all");
          }
        } else {
          // Check if it's a PID (number)
          try {
            size_t pos;
            int pid = stoi(arg, &pos);
            if (pos == arg.length()) {
              process_manager_.stopProcess(pid);
            } else {
              // Not a number, treat as endpoint
              router_client_.stopNode(arg);
            }
          } catch (...) {
            // Conversion failed, treat as endpoint
            router_client_.stopNode(arg);
          }
        }
      } else {
        Logger::getInstance().warn(
            "AdminConsole", "Usage: stop <pid|endpoint> OR stop pids all");
      }
    } else if (cmd == "attach") {
      int pid;
      if (ss >> pid) {
        process_manager_.attachProcess(pid);
      } else {
        Logger::getInstance().warn("AdminConsole", "Usage: attach <pid>");
      }
    } else if (cmd == "clear") {
      system("clear");
    } else if (cmd == "status") {
      if (router_client_.ping()) {
        listNodes();
      } else {
        Logger::getInstance().error("AdminConsole", "Router is OFFLINE.");
      }
    } else if (cmd == "node-status") {
      string endpoint;
      ss >> endpoint;
      if (!endpoint.empty()) {
        handleNodeStatus(endpoint);
      } else {
        Logger::getInstance().warn("AdminConsole",
                                   "Usage: node-status <endpoint>");
      }
    } else if (cmd == "node-keys") {
      string endpoint;
      int limit = 100;
      ss >> endpoint;
      if (!endpoint.empty()) {
        if (ss >> limit) {
        } // optional limit
        handleNodeKeys(endpoint, limit);
      } else {
        Logger::getInstance().warn("AdminConsole",
                                   "Usage: node-keys <endpoint> [limit]");
      }
    } else if (cmd == "node-inspect") {
      string endpoint, key;
      ss >> endpoint >> key;
      if (!endpoint.empty() && !key.empty()) {
        handleNodeInspect(endpoint, key);
      } else {
        Logger::getInstance().warn("AdminConsole",
                                   "Usage: node-inspect <endpoint> <key>");
      }
    } else if (cmd == "ring-check") {
      string endpoint;
      ss >> endpoint;
      if (!endpoint.empty()) {
        handleRingCheck(endpoint);
      } else {
        Logger::getInstance().warn("AdminConsole",
                                   "Usage: ring-check <endpoint>");
      }
    } else {
      Logger::getInstance().warn("AdminConsole", "Unknown command: " + cmd);
    }
  }

  void listNodes() {
    vector<string> nodes = router_client_.getNodes();
    Logger::getInstance().info(
        "AdminConsole", "Active Nodes (" + to_string(nodes.size()) + "):");
    for (const auto &node : nodes) {
      cout << " - " << node << endl;
    }
  }

  void handleNodeStatus(const string &endpoint) {
    Message msg = createNodeStatusMessage("admin_client");
    auto resp = router_client_.sendDirectRequest(endpoint, msg);
    if (resp && resp->payload.value("status", 0) == 200) {
      cout << "Node Status for " << endpoint << ":" << endl;
      cout << resp->payload.dump(2) << endl;
    } else {
      Logger::getInstance().error("AdminConsole",
                                  "Failed to get status from " + endpoint);
    }
  }

  void handleNodeKeys(const string &endpoint, int limit) {
    Message msg = createGetStorageKeysMessage("admin_client", limit);
    auto resp = router_client_.sendDirectRequest(endpoint, msg);
    if (resp && resp->payload.value("status", 0) == 200) {
      cout << "Keys on " << endpoint << ":" << endl;
      if (resp->payload.contains("data")) {
        cout << resp->payload["data"].dump(2) << endl;
      }
    } else {
      Logger::getInstance().error("AdminConsole",
                                  "Failed to get keys from " + endpoint);
    }
  }

  void handleNodeInspect(const string &endpoint, const string &key) {
    Message msg = createGetStorageValueMessage("admin_client", key);
    auto resp = router_client_.sendDirectRequest(endpoint, msg);
    if (resp) {
      int status = resp->payload.value("status", 0);
      if (status == 200) {
        cout << "Value for '" << key << "' on " << endpoint << ":" << endl;
        if (resp->payload.contains("data")) {
          cout << resp->payload["data"].dump(2) << endl;
        }
      } else if (status == 404) {
        cout << "Key '" << key << "' not found on " << endpoint << endl;
      } else {
        Logger::getInstance().error("AdminConsole", "Error inspecting key: " +
                                                        to_string(status));
      }
    } else {
      Logger::getInstance().error("AdminConsole",
                                  "Failed to inspect key on " + endpoint);
    }
  }

  void handleRingCheck(const string &endpoint) {
    Message msg = createGetRingStateMessage("admin_client");
    auto resp = router_client_.sendDirectRequest(endpoint, msg);
    if (resp && resp->payload.value("status", 0) == 200) {
      cout << "Ring state from view of " << endpoint << ":" << endl;
      if (resp->payload.contains("data")) {
        // Formatting helpful output would be nice, but dumping JSON is okay for
        // now
        cout << resp->payload["data"].dump(2) << endl;
      }
    } else {
      Logger::getInstance().error("AdminConsole",
                                  "Failed to get ring state from " + endpoint);
    }
  }

  void printHelp() {
    Logger::getInstance().info("AdminConsole", "Available commands:");
    cout << "  list nodes      - List nodes registered in the ring" << endl;
    cout << "  list routers    - List all managed router processes" << endl;
    cout << "  list pids       - List all managed processes (PID and Type)"
         << endl;
    cout << "  start router    - Start a new router process" << endl;
    cout << "  start node <port> - Start a new node process on specific port"
         << endl;
    cout << "  stop <pid|addr> - Stop a process by PID or Node by Address"
         << endl;
    cout << "  stop pids all   - Stop all managed processes" << endl;
    cout << "  attach <pid>    - Attach to process output (Ctrl+C to detach)"
         << endl;
    cout << "  status          - Check router connection status and list nodes"
         << endl;
    cout
        << "  node-status <endpoint> - Get detailed status from a specific node"
        << endl;
    cout << "  node-keys <endpoint> [limit] - List keys stored on a node"
         << endl;
    cout << "  node-inspect <endpoint> <key> - Inspect a specific key's value "
            "on a node"
         << endl;
    cout << "  ring-check <endpoint> - Get ring state from the view of a "
            "specific node"
         << endl;
    cout << "  clear           - Clear console" << endl;
    cout << "  help            - Show this help message" << endl;
    cout << "  exit            - Exit the console" << endl;
  }
};

#endif // ADMIN_CONSOLE_H

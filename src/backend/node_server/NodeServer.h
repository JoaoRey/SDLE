#ifndef NODE_SERVER_H
#define NODE_SERVER_H

#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <uuid/uuid.h>
#include <vector>
#include <zmq.hpp>

#include "../common/Metadata.h"
#include "../common/Protocol.h"
#include "../common/VNode.h"
#include "../crdt/CRDTShoppingList.h"
#include "Storage.h"

using namespace std;

class NodeServer {
private:
  static constexpr size_t N = 3; // Replication factor
  static constexpr size_t R = 2; // Read quorum
  static constexpr size_t W = 2; // Write quorum
  static constexpr size_t DEFAULT_VNODES = 4;
  static constexpr int HEARTBEAT_MS = 5000;
  static constexpr int REQUEST_TIMEOUT_MS = 3000;

  // Identity
  string node_id_;
  string endpoint_;
  int port_;
  int heartbeat_interval_ms_;
  vector<VNode> vnodes_;

  zmq::context_t context_;
  unique_ptr<zmq::socket_t> server_socket_; // ROUTER for incoming requests
  unique_ptr<zmq::socket_t> router_socket_; // DEALER to communicate with router

  // State
  Storage storage_;
  Metadata metadata_;
  string router_endpoint_;
  string data_dir_;

  atomic<bool> running_{false};
  thread server_thread_;
  thread heartbeat_thread_;
  thread sync_thread_;
  atomic<bool> sync_in_progress_{false};
  mutex socket_mutex_;

  // Quorum tracking
  struct PendingRequest {
    string request_id;
    MessageType type;
    size_t expected_responses;
    size_t received_responses;
    vector<nlohmann::json> responses;
    std::promise<vector<nlohmann::json>> result_promise;
    chrono::steady_clock::time_point deadline;
  };

  map<string, shared_ptr<PendingRequest>> pending_requests_;
  mutex pending_mutex_;

public:
  NodeServer();

  ~NodeServer();

  bool initialize(const string &config_path, int port_override = -1,
                  const string &router_override = "");

  void run();

  void stop();

  string getEndpoint() const { return endpoint_; }
  string getNodeId() const { return node_id_; }

private:
  string generateUUID();

  void createVNodes(size_t count);

  bool registerWithRouter();

  void sendToRouter(const Message &msg);

  /// Thread to send heartbeat to router
  void heartbeatLoop();

  /// Main thread loop
  void serverLoop();

  void handleServerMessage();

  void handleRouterMessage();

  void handleCoordinatorRequest(const Message &msg);

  /// GET request - quorum read
  Message coordinateGet(const Message &msg);

  /// PUT request - quorum write
  Message coordinatePut(const Message &msg);

  /// DELETE request
  Message coordinateDelete(const Message &msg);

  /// Handle GET request - current node is replica
  Message handleGet(const Message &msg);

  /// Handle PUT request - current node is replica
  Message handlePut(const Message &msg);

  /// Handl DELETE request - current node is replica
  Message handleDelete(const Message &msg);

  /// REPLICATE request - current node is replica
  Message handleReplicate(const Message &msg);

  /// Repairs the replicas - leader sent read repair
  void handleReadRepair(const Message &msg);

  void handleResponseMessage(const Message &msg);

  // Debug / Admin handlers
  Message handleNodeStatus(const Message &msg);
  Message handleGetStorageKeys(const Message &msg);
  Message handleGetStorageValue(const Message &msg);
  Message handleGetRingState(const Message &msg);

  /// optional beacause read quorum may fail
  /// Retrieves a shopping list from a replica node
  optional<CRDTShoppingList> sendGetToNode(const string &node_endpoint,
                                           const string &list_name,
                                           const string &req_id);

  bool sendReplicateToNode(const string &node_endpoint, const string &list_name,
                           const CRDTShoppingList &list, const string &req_id);

  bool sendDeleteToNode(const string &node_endpoint, const string &list_name,
                        const string &req_id);

  /// Send read repair to replicas
  void performReadRepair(const string &list_name,
                         const CRDTShoppingList &merged,
                         const vector<string> &nodes,
                         const vector<optional<CRDTShoppingList>> &responses);

  void cleanupPendingRequests();

  /// Handle replica synchronization when topology changes
  void handleTopologyChange(const Metadata &old_metadata,
                           const Metadata &new_metadata);

  /// Compare preference lists
  vector<string> findNewNodes(const vector<string> &old_list,
                             const vector<string> &new_list);

  /// Check if endpoint is in the list
  bool contains(const vector<string> &list, const string &endpoint);

  /// Sync data from replicas for a specific key
  void syncFromReplicas(const string &key, const vector<string> &replicas);

  /// Get candidate nodes for syncN predecessors + N successors
  vector<string> getCandidateNodesForSync(const Metadata &metadata);

  /// Determine if full sync should be performed
  bool shouldPerformFullSync(const Metadata &old_metadata,
                            const Metadata &new_metadata);

  /// Request data sync from a specific node
  void requestSyncFromNode(const string &node_endpoint);

  Message handleSyncRequest(const Message &msg);

  void processSyncResponse(const Message &response);
};

#endif
#include "NodeServer.h"

#include "../common/Logger.h" // Added Logger.h
#include <fstream>
#include <iostream>
#include <set>

using namespace std;

NodeServer::NodeServer() : context_(1) {}

NodeServer::~NodeServer() { stop(); }

bool NodeServer::initialize(const string &config_path, int port_override,
                            const string &router_override) {
  Logger::getInstance().info("NodeServer", "Initializing node server...");

  ifstream config_file(config_path);
  if (!config_file.is_open()) {
    Logger::getInstance().error("NodeServer",
                                "Failed to open config file: " + config_path);
    return false;
  }

  nlohmann::json config;
  try {
    config_file >> config;
  } catch (const exception &e) {
    Logger::getInstance().error("NodeServer",
                                "Failed to parse config: " + string(e.what()));
    return false;
  }

  // Load from config
  node_id_ = config.value("node_id", generateUUID());
  port_ = config.value("port", 8081);
  router_endpoint_ = config.value("router_endpoint", "tcp://127.0.0.1:5555");
  data_dir_ = config.value("data_dir", "data");
  heartbeat_interval_ms_ = config.value("heartbeat_interval_ms", 5000);
  size_t num_vnodes = config.value("vnodes", DEFAULT_VNODES);

  // Apply Overrides
  if (port_override != -1) {
    port_ = port_override;
    Logger::getInstance().info("NodeServer",
                               "Correcting port from CLI: " + to_string(port_));
  }
  if (!router_override.empty()) {
    router_endpoint_ = router_override;
    Logger::getInstance().info("NodeServer", "Correcting router from CLI: " +
                                                 router_endpoint_);
  }

  // Endpoint is derived from port
  endpoint_ = "tcp://127.0.0.1:" + to_string(port_);

  Logger::getInstance().info("NodeServer", "Node ID: " + node_id_);

  // Create server socket
  server_socket_ =
      make_unique<zmq::socket_t>(context_, zmq::socket_type::router);

  server_socket_->set(zmq::sockopt::linger, 0);
  string bind_addr = "tcp://*:" + to_string(port_);
  try {
    server_socket_->bind(bind_addr);
    Logger::getInstance().info("NodeServer", "Bound to: " + bind_addr);
  } catch (const zmq::error_t &e) {
    Logger::getInstance().error("NodeServer", "Failed to bind to " + bind_addr +
                                                  ": " + e.what());
    return false;
  }

  // Get the port
  endpoint_ = server_socket_->get(zmq::sockopt::last_endpoint, 256);

  size_t colon_pos = endpoint_.rfind(':');
  if (colon_pos != string::npos) {
    port_ = stoi(endpoint_.substr(colon_pos + 1));
  }

  endpoint_ = "tcp://127.0.0.1:" + to_string(port_);
  Logger::getInstance().info("NodeServer", "Bound to: " + endpoint_);

  createVNodes(num_vnodes);

  if (!storage_.open(data_dir_, node_id_)) {
    Logger::getInstance().error("NodeServer", "Failed to initialize storage");
    return false;
  }

  // Create router socket
  router_socket_ =
      make_unique<zmq::socket_t>(context_, zmq::socket_type::dealer);
  router_socket_->set(zmq::sockopt::linger, 0);
  string identity = node_id_;
  // Set identity to identify for router
  router_socket_->set(zmq::sockopt::routing_id, identity);

  return true;
}

void NodeServer::run() {
  running_ = true;

  // Connect to router and register
  try {
    router_socket_->connect(router_endpoint_);
    Logger::getInstance().info("NodeServer",
                               "Connected to router at " + router_endpoint_);

    if (!registerWithRouter()) {
      Logger::getInstance().error("NodeServer",
                                  "Failed to register with router");
    }
  } catch (const exception &e) {
    Logger::getInstance().error("NodeServer", "Failed to connect to router: " +
                                                  string(e.what()));
  }

  heartbeat_thread_ = thread(&NodeServer::heartbeatLoop, this);

  server_thread_ = thread(&NodeServer::serverLoop, this);

  Logger::getInstance().info("NodeServer", "Node server running...");

  if (server_thread_.joinable()) {
    server_thread_.join();
  }
}

void NodeServer::stop() {
  if (!running_)
    return;

  try {
    Message unreg = createUnregisterMessage(endpoint_);
    sendToRouter(unreg);
  } catch (...) {
    // Ignore errors during shutdown
  }

  running_ = false;
  if (heartbeat_thread_.joinable()) {
    heartbeat_thread_.join();
  }
  if (sync_thread_.joinable()) {
    sync_thread_.join();
  }

  // Now safe to close sockets
  if (server_socket_) {
    server_socket_->close();
  }
  if (router_socket_) {
    router_socket_->close();
  }

  storage_.close();
  Logger::getInstance().info("NodeServer", "Node server stopped");
}

string NodeServer::generateUUID() {
  uuid_t id;
  uuid_generate(id);
  char buf[37];
  uuid_unparse_lower(id, buf);
  return string(buf);
}

void NodeServer::createVNodes(size_t count) {
  vnodes_.clear();
  for (size_t i = 0; i < count; ++i) {
    string vnode_id = node_id_ + ":" + to_string(i);
    uint64_t position = HashRing::hash(vnode_id);
    vnodes_.emplace_back(vnode_id, endpoint_, position);
  }

  metadata_.addNode(endpoint_, vnodes_);

  Logger::getInstance().info("NodeServer",
                             "Created " + to_string(count) + " vnodes");
}

bool NodeServer::registerWithRouter() {
  Message reg = createRegisterMessage(endpoint_, vnodes_);
  sendToRouter(reg);

  // Wait for REGISTER_ACK with timeout
  zmq::pollitem_t items[] = {{*router_socket_, 0, ZMQ_POLLIN, 0}};
  int rc = zmq::poll(items, 1, chrono::milliseconds(REQUEST_TIMEOUT_MS));

  if (rc > 0 && (items[0].revents & ZMQ_POLLIN)) {
    zmq::message_t reply;
    auto result = router_socket_->recv(reply, zmq::recv_flags::none);
    if (result) {
      try {
        string data(static_cast<char *>(reply.data()), reply.size());
        Message msg = Message::deserialize(data);

        if (msg.type == MessageType::REGISTER_ACK) {
          // Update local metadata with state from router
          if (msg.payload.contains("metadata")) {
            metadata_.fromJson(msg.payload["metadata"]);
          }
          Logger::getInstance().info(
              "NodeServer", "Registered with router, ring has " +
                                to_string(metadata_.nodeCount()) + " nodes");
          return true;
        }
      } catch (const exception &e) {
        Logger::getInstance().error("NodeServer",
                                    "Failed to parse registration response: " +
                                        string(e.what()));
      }
    }
  }

  return false;
}

void NodeServer::sendToRouter(const Message &msg) {
  string data = msg.serialize();
  zmq::message_t zmq_msg(data.size());
  memcpy(zmq_msg.data(), data.c_str(), data.size());

  lock_guard<mutex> lock(socket_mutex_);
  router_socket_->send(zmq_msg, zmq::send_flags::none);
}

void NodeServer::heartbeatLoop() {
  while (running_) {
    this_thread::sleep_for(chrono::milliseconds(heartbeat_interval_ms_));

    if (!running_)
      break;

    try {
      Message hb = createHeartbeatMessage(endpoint_);
      sendToRouter(hb);
    } catch (const exception &e) {
      Logger::getInstance().error("NodeServer",
                                  "Heartbeat failed: " + string(e.what()));
    }
  }
}

void NodeServer::serverLoop() {
  zmq::pollitem_t items[] = {{*server_socket_, 0, ZMQ_POLLIN, 0},
                             {*router_socket_, 0, ZMQ_POLLIN, 0}};

  while (running_) {
    try {
      int return_code = zmq::poll(items, 2, chrono::milliseconds(100));

      if (return_code < 0)
        break;

      // Messages from other nodes
      if (items[0].revents & ZMQ_POLLIN) {
        handleServerMessage();
      }

      // Messages from router
      if (items[1].revents & ZMQ_POLLIN) {
        handleRouterMessage();
      }

      cleanupPendingRequests();
    } catch (const zmq::error_t &e) {
      if (e.num() != ETERM && running_) {
        Logger::getInstance().error("NodeServer", "ZMQ error in server loop: " +
                                                      string(e.what()));
      }
    }
  }
}

void NodeServer::handleServerMessage() {
  zmq::message_t identity;
  zmq::message_t empty;
  zmq::message_t content;

  (void)server_socket_->recv(identity, zmq::recv_flags::none);
  (void)server_socket_->recv(empty, zmq::recv_flags::none);
  zmq::recv_result_t result =
      server_socket_->recv(content, zmq::recv_flags::none);
  if (!result)
    return;

  try {
    string data(static_cast<char *>(content.data()), content.size());
    Message msg = Message::deserialize(data);
    Logger::getInstance().info("NodeServer", "Received server message: " +
                                                 messageTypeToString(msg.type));

    Message response;

    switch (msg.type) {
    case MessageType::GET:
      response = handleGet(msg);
      break;
    case MessageType::PUT:
      response = handlePut(msg);
      break;
    case MessageType::DELETE:
      response = handleDelete(msg);
      break;
    case MessageType::REPLICATE:
      response = handleReplicate(msg);
      break;
    case MessageType::READ_REPAIR:
      handleReadRepair(msg);
      return;
    case MessageType::NODE_STATUS:
      response = handleNodeStatus(msg);
      break;
    case MessageType::GET_STORAGE_KEYS:
      response = handleGetStorageKeys(msg);
      break;
    case MessageType::GET_STORAGE_VALUE:
      response = handleGetStorageValue(msg);
      break;
    case MessageType::GET_RING_STATE:
      response = handleGetRingState(msg);
      break;
    case MessageType::SYNC_REQUEST:
      response = handleSyncRequest(msg);
      break;
    case MessageType::RESPONSE:
      handleResponseMessage(msg);
      return;
    default:
      response = createResponseMessage(endpoint_, StatusCode::INTERNAL_ERROR,
                                       msg.request_id);
      Logger::getInstance().warn("NodeServer", "Unknown message type from " +
                                                   msg.sender_endpoint);
      break;
    }

    string reply_data = response.serialize();
    Logger::getInstance().info("NodeServer",
                               "Sending response to " + msg.sender_endpoint);
    // Send response back
    // Identity + empty frame + content
    server_socket_->send(identity, zmq::send_flags::sndmore);
    server_socket_->send(zmq::message_t(), zmq::send_flags::sndmore);
    server_socket_->send(zmq::message_t(reply_data.data(), reply_data.size()),
                         zmq::send_flags::none);
    Logger::getInstance().info("NodeServer", "Response sent");
  } catch (const exception &e) {
    Logger::getInstance().error(
        "NodeServer", "Error handling server message: " + string(e.what()));
  }
}

void NodeServer::handleRouterMessage() {
  zmq::message_t content;
  auto result = router_socket_->recv(content, zmq::recv_flags::none);

  if (!result)
    return;

  try {
    string data(static_cast<char *>(content.data()), content.size());
    Message msg = Message::deserialize(data);
    Logger::getInstance().info("NodeServer", "Received message from router: " +
                                                 messageTypeToString(msg.type));

    switch (msg.type) {
    case MessageType::SYNC_METADATA:
      if (msg.payload.contains("metadata")) {
        nlohmann::json old_metadata_json = metadata_.toJson();
        
        metadata_.fromJson(msg.payload["metadata"]);
        nlohmann::json new_metadata_json = metadata_.toJson();
        
        Logger::getInstance().info(
            "NodeServer", "Metadata updated, ring has " +
                              to_string(metadata_.nodeCount()) + " nodes");
        // Trigger replica synchronization if needed
        if (!sync_in_progress_.exchange(true)) {
          if (sync_thread_.joinable()) {
            sync_thread_.join();
          }
          sync_thread_ = thread([this, old_metadata_json, new_metadata_json]() {
            Metadata old_metadata;
            Metadata new_metadata;
            old_metadata.fromJson(old_metadata_json);
            new_metadata.fromJson(new_metadata_json);
            handleTopologyChange(old_metadata, new_metadata);
            sync_in_progress_ = false;
          });
          sync_thread_.detach();
        } else {
          Logger::getInstance().warn("NodeServer", 
                                     "Sync already in progress, skipping concurrent sync");
        }
      }
      break;
    case MessageType::GET:
    case MessageType::PUT:
    case MessageType::DELETE:
      // Router has decided this node is coordinator
      handleCoordinatorRequest(msg);
      break;
    case MessageType::STOP_NODE:
      Logger::getInstance().warn(
          "NodeServer",
          "Received STOP_NODE command from router. Shutting down...");
      stop();
      break;
    default:
      break;
    }
  } catch (const exception &e) {
    Logger::getInstance().error(
        "NodeServer", "Error handling router message: " + string(e.what()));
  }
}

void NodeServer::handleCoordinatorRequest(const Message &msg) {
  Message response;

  switch (msg.type) {
  case MessageType::GET:
    response = coordinateGet(msg);
    break;
  case MessageType::PUT:
    response = coordinatePut(msg);
    break;
  case MessageType::DELETE:
    response = coordinateDelete(msg);
    break;
  default:
    response = createResponseMessage(endpoint_, StatusCode::INTERNAL_ERROR,
                                     msg.request_id);
    break;
  }

  sendToRouter(response);
}

Message NodeServer::coordinateGet(const Message &msg) {
  string list_name = msg.payload.value("list_name", "");
  if (list_name.empty()) {
    // This node is not leader for this shopping list
    return createResponseMessage(endpoint_, StatusCode::NOT_FOUND,
                                 msg.request_id);
  }

  vector<string> nodes = metadata_.getNodesForKey(list_name, N);
  if (nodes.empty()) {
    // No replicas
    return createResponseMessage(endpoint_, StatusCode::UNAVAILABLE,
                                 msg.request_id);
  }

  vector<optional<CRDTShoppingList>> responses;

  for (const string &node_endpoint : nodes) {
    if (node_endpoint == endpoint_) {
      // Local read
      responses.push_back(storage_.load(list_name));
    } else {
      // Replica read
      auto result = sendGetToNode(node_endpoint, list_name, msg.request_id);
      responses.push_back(result);
    }
  }

  size_t success_count = 0;
  optional<CRDTShoppingList> merged_result;

  for (const auto &resp : responses) {
    if (resp.has_value()) {
      success_count++;
      // Merge CRDT
      if (!merged_result.has_value()) {
        merged_result = resp.value();
      } else {
        merged_result->merge(resp.value());
      }
    }
  }

  // Check quorum
  if (success_count < R) {
    return createResponseMessage(endpoint_, StatusCode::UNAVAILABLE,
                                 msg.request_id);
  }
  // Read quorum happened
  if (merged_result.has_value()) {
    performReadRepair(list_name, merged_result.value(), nodes, responses);

    nlohmann::json data = merged_result.value();
    return createResponseMessage(endpoint_, StatusCode::OK, msg.request_id,
                                 data);
  }

  return createResponseMessage(endpoint_, StatusCode::NOT_FOUND,
                               msg.request_id);
}

Message NodeServer::coordinatePut(const Message &msg) {
  Logger::getInstance().info("NodeServer",
                             "Coordinating PUT for request " + msg.request_id);
  string list_name = msg.payload.value("list_name", "");
  if (list_name.empty() || !msg.payload.contains("data")) {
    return createResponseMessage(endpoint_, StatusCode::INTERNAL_ERROR,
                                 msg.request_id);
  }

  CRDTShoppingList new_list = msg.payload["data"].get<CRDTShoppingList>();

  // Get preference list
  vector<string> nodes = metadata_.getNodesForKey(list_name, N);

  if (nodes.empty()) {
    // No replicas
    return createResponseMessage(endpoint_, StatusCode::UNAVAILABLE,
                                 msg.request_id);
  }

  // Merge CRDT
  auto existing = storage_.load(list_name);
  if (existing.has_value()) {
    new_list.merge(existing.value());
  }

  // Set this node ID - client has no knowledge
  // of node ring structure
  new_list.set_node_id(node_id_);

  bool is_deleted = !new_list.hasActiveItems();

  size_t success_count = 0;

  for (const string &node_endpoint : nodes) {
    bool success = false;

    if (node_endpoint == endpoint_) {
      Logger::getInstance().info("NodeServer", "Saving locally...");
      success = storage_.save(list_name, new_list, is_deleted);
    } else {
      Logger::getInstance().info("NodeServer",
                                 "Replicating to " + node_endpoint + "...");
      // Replicate
      success = sendReplicateToNode(node_endpoint, list_name, new_list,
                                    msg.request_id);
      Logger::getInstance().info("NodeServer",
                                 "Replication to " + node_endpoint +
                                     (success ? " succeeded" : " failed"));
    }

    if (success) {
      success_count++;
    }
  }

  // Check write quorum
  if (success_count >= W) {
    nlohmann::json data = new_list;
    return createResponseMessage(endpoint_, StatusCode::OK, msg.request_id,
                                 data);
  }

  return createResponseMessage(endpoint_, StatusCode::UNAVAILABLE,
                               msg.request_id);
}

Message NodeServer::coordinateDelete(const Message &msg) {
  string list_name = msg.payload.value("list_name", "");
  if (list_name.empty()) {
    return createResponseMessage(endpoint_, StatusCode::NOT_FOUND,
                                 msg.request_id);
  }

  vector<string> nodes = metadata_.getNodesForKey(list_name, N);

  size_t success_count = 0;

  for (const string &node_endpoint : nodes) {
    bool success = false;

    if (node_endpoint == endpoint_) {
      success = storage_.remove(list_name);
    } else {
      success = sendDeleteToNode(node_endpoint, list_name, msg.request_id);
    }

    if (success) {
      success_count++;
    }
  }

  if (success_count >= W) {
    return createResponseMessage(endpoint_, StatusCode::OK, msg.request_id);
  }

  return createResponseMessage(endpoint_, StatusCode::UNAVAILABLE,
                               msg.request_id);
}

Message NodeServer::handleGet(const Message &msg) {
  string list_name = msg.payload.value("list_name", "");
  auto result = storage_.load(list_name);

  if (result.has_value()) {
    nlohmann::json data = result.value();
    return createResponseMessage(endpoint_, StatusCode::OK, msg.request_id,
                                 data);
  }

  return createResponseMessage(endpoint_, StatusCode::NOT_FOUND,
                               msg.request_id);
}

Message NodeServer::handlePut(const Message &msg) {
  string list_name = msg.payload.value("list_name", "");
  if (!msg.payload.contains("data")) {
    return createResponseMessage(endpoint_, StatusCode::INTERNAL_ERROR,
                                 msg.request_id);
  }

  CRDTShoppingList new_list = msg.payload["data"].get<CRDTShoppingList>();

  // Merge with existing if present
  auto existing = storage_.load(list_name);
  if (existing.has_value()) {
    new_list.merge(existing.value());
  }

  bool is_deleted = !new_list.hasActiveItems();

  if (storage_.save(list_name, new_list, is_deleted)) {
    nlohmann::json data = new_list;
    return createResponseMessage(endpoint_, StatusCode::OK, msg.request_id,
                                 data);
  }

  return createResponseMessage(endpoint_, StatusCode::INTERNAL_ERROR,
                               msg.request_id);
}

Message NodeServer::handleDelete(const Message &msg) {
  string list_name = msg.payload.value("list_name", "");

  if (storage_.remove(list_name)) {
    return createResponseMessage(endpoint_, StatusCode::OK, msg.request_id);
  }

  return createResponseMessage(endpoint_, StatusCode::NOT_FOUND,
                               msg.request_id);
}

Message NodeServer::handleReplicate(const Message &msg) {
  Logger::getInstance().info("NodeServer",
                             "Handling REPLICATE for " +
                                 msg.payload.value("list_name", ""));
  string list_name = msg.payload.value("list_name", "");
  if (!msg.payload.contains("data")) {
    return createResponseMessage(endpoint_, StatusCode::INTERNAL_ERROR,
                                 msg.request_id);
  }

  CRDTShoppingList new_list = msg.payload["data"].get<CRDTShoppingList>();
  auto existing = storage_.load(list_name);
  if (existing.has_value()) {
    new_list.merge(existing.value());
  }

  bool is_deleted = !new_list.hasActiveItems();

  if (storage_.save(list_name, new_list, is_deleted)) {
    return createResponseMessage(endpoint_, StatusCode::OK, msg.request_id);
  }

  return createResponseMessage(endpoint_, StatusCode::INTERNAL_ERROR,
                               msg.request_id);
}

void NodeServer::handleReadRepair(const Message &msg) {
  string list_name = msg.payload.value("list_name", "");
  if (!msg.payload.contains("data"))
    return;

  CRDTShoppingList new_list = msg.payload["data"].get<CRDTShoppingList>();

  auto existing = storage_.load(list_name);
  if (existing.has_value()) {
    new_list.merge(existing.value());
  }

  bool is_deleted = !new_list.hasActiveItems();

  storage_.save(list_name, new_list, is_deleted);
}

void NodeServer::handleResponseMessage(const Message &msg) {
  lock_guard<mutex> lock(pending_mutex_);

  auto it = pending_requests_.find(msg.request_id);
  if (it != pending_requests_.end()) {
    auto &pending = it->second;
    pending->responses.push_back(msg.payload);
    pending->received_responses++;

    if (pending->received_responses >= pending->expected_responses) {
      pending->result_promise.set_value(pending->responses);
      pending_requests_.erase(it);
    }
  }
}

optional<CRDTShoppingList>
NodeServer::sendGetToNode(const string &node_endpoint, const string &list_name,
                          const string &req_id) {
  try {
    zmq::socket_t sock(context_, zmq::socket_type::req);
    sock.set(zmq::sockopt::linger, 0);
    sock.set(zmq::sockopt::rcvtimeo, REQUEST_TIMEOUT_MS);
    sock.connect(node_endpoint);

    Message get_msg = createGetMessage(endpoint_, list_name, req_id);
    string data = get_msg.serialize();
    sock.send(zmq::message_t(data.data(), data.size()), zmq::send_flags::none);

    zmq::message_t reply;
    auto result = sock.recv(reply, zmq::recv_flags::none);

    if (result) {
      string reply_data(static_cast<char *>(reply.data()), reply.size());
      Message response = Message::deserialize(reply_data);

      int status = response.payload.value("status", 500);
      if (status == 200 && response.payload.contains("data")) {
        return response.payload["data"].get<CRDTShoppingList>();
      }
    }
  } catch (const exception &e) {
    Logger::getInstance().error("NodeServer", "Failed to get from " +
                                                  node_endpoint + ": " +
                                                  string(e.what()));
  }

  return nullopt;
}

bool NodeServer::sendReplicateToNode(const string &node_endpoint,
                                     const string &list_name,
                                     const CRDTShoppingList &list,
                                     const string &req_id) {
  try {
    zmq::socket_t sock(context_, zmq::socket_type::req);
    sock.set(zmq::sockopt::linger, 0);
    sock.set(zmq::sockopt::rcvtimeo, REQUEST_TIMEOUT_MS);
    sock.connect(node_endpoint);

    nlohmann::json list_json = list;
    Message rep_msg =
        createReplicateMessage(endpoint_, list_name, list_json, req_id);
    string data = rep_msg.serialize();
    sock.send(zmq::message_t(data.data(), data.size()), zmq::send_flags::none);

    zmq::message_t reply;
    auto result = sock.recv(reply, zmq::recv_flags::none);

    if (result) {
      string reply_data(static_cast<char *>(reply.data()), reply.size());
      Message response = Message::deserialize(reply_data);
      int status = response.payload.value("status", 500);
      return status == 200;
    }
  } catch (const exception &e) {
    cerr << "Failed to replicate to " << node_endpoint << ": " << e.what()
         << endl;
  }

  return false;
}

bool NodeServer::sendDeleteToNode(const string &node_endpoint,
                                  const string &list_name,
                                  const string &req_id) {
  try {
    zmq::socket_t sock(context_, zmq::socket_type::req);
    sock.set(zmq::sockopt::linger, 0);
    sock.set(zmq::sockopt::rcvtimeo, REQUEST_TIMEOUT_MS);
    sock.connect(node_endpoint);

    Message del_msg = createDeleteMessage(endpoint_, list_name, req_id);
    string data = del_msg.serialize();
    sock.send(zmq::message_t(data.data(), data.size()), zmq::send_flags::none);

    zmq::message_t reply;
    auto result = sock.recv(reply, zmq::recv_flags::none);

    if (result) {
      string reply_data(static_cast<char *>(reply.data()), reply.size());
      Message response = Message::deserialize(reply_data);
      int status = response.payload.value("status", 500);
      return status == 200;
    }
  } catch (const exception &e) {
    cerr << "Failed to delete from " << node_endpoint << ": " << e.what()
         << endl;
  }

  return false;
}

void NodeServer::performReadRepair(
    const string &list_name, const CRDTShoppingList &merged,
    const vector<string> &nodes,
    const vector<optional<CRDTShoppingList>> &responses) {
  // CRDT obtained from successful read quorum
  nlohmann::json merged_json = merged;

  for (size_t i = 0; i < nodes.size() && i < responses.size(); ++i) {
    if (!responses[i].has_value())
      continue;

    // Check if CRDT for node i differs
    nlohmann::json node_json = responses[i].value();
    if (node_json != merged_json) {
      Logger::getInstance().info("NodeServer",
                                 "Read repair: updating " + nodes[i]);
      // Send read repair
      try {
        zmq::socket_t sock(context_, zmq::socket_type::dealer);
        sock.set(zmq::sockopt::linger, 0);
        sock.connect(nodes[i]);

        Message rr_msg =
            createReadRepairMessage(endpoint_, list_name, merged_json);
        string data = rr_msg.serialize();
        sock.send(zmq::message_t(data.data(), data.size()),
                  zmq::send_flags::none);
      } catch (const exception &e) {
        Logger::getInstance().error("NodeServer", "Read repair failed for " +
                                                      nodes[i] + ": " +
                                                      string(e.what()));
      }
    }
  }
}

void NodeServer::cleanupPendingRequests() {
  lock_guard<mutex> lock(pending_mutex_);
  auto now = chrono::steady_clock::now();

  for (auto it = pending_requests_.begin(); it != pending_requests_.end();) {
    if (now > it->second->deadline) {
      Logger::getInstance().warn("NodeServer",
                                 "Request " + it->first + " timed out");
      // Could notify waiting thread here if we used condition variables
      // But we use promises, so maybe set exception?
      // For now, just remove
      it = pending_requests_.erase(it);
    } else {
      ++it;
    }
  }
}

Message NodeServer::handleNodeStatus(const Message &msg) {
  nlohmann::json status;
  status["node_id"] = node_id_;
  status["endpoint"] = endpoint_;
  status["vnodes_count"] = vnodes_.size();

  {
    lock_guard<mutex> lock(pending_mutex_);
    status["pending_requests"] = pending_requests_.size();
  }

  status["storage_keys_count"] = storage_.count();

  return createResponseMessage(endpoint_, StatusCode::OK, msg.request_id,
                               status);
}

Message NodeServer::handleGetStorageKeys(const Message &msg) {
  int limit = msg.payload.value("limit", 100);
  vector<string> keys = storage_.getKeys(limit);

  nlohmann::json data;
  data["keys"] = keys;
  data["count"] = keys.size();

  return createResponseMessage(endpoint_, StatusCode::OK, msg.request_id, data);
}

Message NodeServer::handleGetStorageValue(const Message &msg) {
  string key = msg.payload.value("key", "");
  auto list = storage_.load(key);

  if (list.has_value()) {
    nlohmann::json data = list.value();
    return createResponseMessage(endpoint_, StatusCode::OK, msg.request_id,
                                 data);
  }

  return createResponseMessage(endpoint_, StatusCode::NOT_FOUND,
                               msg.request_id);
}

Message NodeServer::handleGetRingState(const Message &msg) {
  nlohmann::json param = metadata_.toJson();
  return createResponseMessage(endpoint_, StatusCode::OK, msg.request_id,
                               param);
}

void NodeServer::handleTopologyChange(const Metadata &old_metadata,
                                      const Metadata &new_metadata) {
  Logger::getInstance().info("NodeServer",
                            "Handling topology change, checking replicas...");

  vector<string> all_keys = storage_.getKeys(-1); // Get all keys

  for (const string &key : all_keys) {
    vector<string> old_replicas = old_metadata.getNodesForKey(key, N);
    vector<string> new_replicas = new_metadata.getNodesForKey(key, N);

    bool was_replica = contains(old_replicas, endpoint_);
    bool is_replica = contains(new_replicas, endpoint_);

    if (!was_replica && is_replica) {
      // New replica - fetch from existing replicas (fallback)
      Logger::getInstance().info("NodeServer", "Newly responsible for key '" +
                                                  key + "', fetching data...");
      syncFromReplicas(key, new_replicas);
    } else if (was_replica) {
      vector<string> new_peers = findNewNodes(old_replicas, new_replicas);

      if (!new_peers.empty()) {
        // Push data to new replicas - faster consistency
        Logger::getInstance().info(
            "NodeServer",
            "Pushing key '" + key + "' to " + to_string(new_peers.size()) +
                " new peer" + (is_replica ? "" : " (no longer replica)"));

        auto data = storage_.load(key);
        if (data.has_value()) {
          for (const string &new_peer : new_peers) {
            if (new_peer != endpoint_) {
              string req_id = generateUUID();
              bool success =
                  sendReplicateToNode(new_peer, key, data.value(), req_id);
              Logger::getInstance().info(
                  "NodeServer", "Push to " + new_peer +
                                    (success ? " succeeded" : " failed"));
            }
          }
        }
      }
    }
  }

  // Pull from neighboors
  if (shouldPerformFullSync(old_metadata, new_metadata)) {
    Logger::getInstance().info("NodeServer",
                              "Performing full sync from candidate nodes...");

    vector<string> candidates = getCandidateNodesForSync(new_metadata);
    Logger::getInstance().info("NodeServer", "Identified " +
                                                to_string(candidates.size()) +
                                                " candidate nodes for sync");

    for (const string &candidate : candidates) {
      if (candidate != endpoint_) {
        requestSyncFromNode(candidate);
      }
    }
  }

  Logger::getInstance().info("NodeServer",
                            "Topology change handling complete");
}

vector<string> NodeServer::findNewNodes(const vector<string> &old_list,
                                       const vector<string> &new_list) {
  vector<string> new_nodes;
  for (const string &node : new_list) {
    if (find(old_list.begin(), old_list.end(), node) == old_list.end()) {
      new_nodes.push_back(node);
    }
  }
  return new_nodes;
}

bool NodeServer::contains(const vector<string> &list, const string &endpoint) {
  return find(list.begin(), list.end(), endpoint) != list.end();
}

/// Given nodes try to push for new replicas this is not necessary
/// but helps with faster consistency. CRDTs make it safe since 
/// they are idempotent
void NodeServer::syncFromReplicas(const string &key,
                                  const vector<string> &replicas) {
  Logger::getInstance().info("NodeServer",
                            "Syncing key '" + key + "' from replicas");

  vector<optional<CRDTShoppingList>> responses;

  for (const string &node_endpoint : replicas) {
    if (node_endpoint == endpoint_) {
      continue;
    } else {
      // Fetch from replica
      string req_id = generateUUID();
      auto result = sendGetToNode(node_endpoint, key, req_id);
      responses.push_back(result);
    }
  }

  optional<CRDTShoppingList> merged_result;
  size_t success_count = 0;

  for (const auto &resp : responses) {
    if (resp.has_value()) {
      success_count++;
      if (!merged_result.has_value()) {
        merged_result = resp.value();
      } else {
        merged_result->merge(resp.value());
      }
    }
  }

  if (success_count > 0 && merged_result.has_value()) {
    // Save merged data locally
    merged_result->set_node_id(node_id_);
    bool is_deleted = !merged_result->hasActiveItems();
    bool saved = storage_.save(key, merged_result.value(), is_deleted);

    Logger::getInstance().info("NodeServer",
                              "Synced key '" + key + "' from " +
                                  to_string(success_count) + " replica(s), " +
                                  (saved ? "saved successfully" : "save failed"));
  } else {
    Logger::getInstance().warn("NodeServer",
                              "Failed to sync key '" + key +
                                  "' - no replicas responded");
  }
}

vector<string>
NodeServer::getCandidateNodesForSync(const Metadata &metadata) {
  std::set<string> candidates;

  for (const auto &vnode : vnodes_) {
    vector<string> neighbors =
        metadata.getNeighborNodesForVNode(vnode.position, N);

    for (const string &neighbor : neighbors) {
      if (neighbor != endpoint_) {
        candidates.insert(neighbor);
      }
    }
  }

  return {candidates.begin(), candidates.end()};
}

bool NodeServer::shouldPerformFullSync(const Metadata &old_metadata,
                                       const Metadata &new_metadata) {
  // First join - no metadata - not know what to sync
  if (old_metadata.nodeCount() == 0) {
    return true;
  }

  // More than 50% change
  // Only because we are testing with small number of nodes
  // Preferable- 20% 
  int old_count = static_cast<int>(old_metadata.nodeCount());
  int new_count = static_cast<int>(new_metadata.nodeCount());
  int delta = abs(new_count - old_count);

  return (delta > old_count / 2);
}

void NodeServer::requestSyncFromNode(const string &node_endpoint) {
  try {
    Logger::getInstance().info("NodeServer",
                              "Requesting sync from " + node_endpoint);

    zmq::socket_t sock(context_, zmq::socket_type::req);
    sock.set(zmq::sockopt::linger, 0);
    sock.set(zmq::sockopt::rcvtimeo, REQUEST_TIMEOUT_MS);
    sock.connect(node_endpoint);

    string req_id = generateUUID();
    Message sync_msg = createSyncRequestMessage(endpoint_, req_id);
    string data = sync_msg.serialize();
    sock.send(zmq::message_t(data.data(), data.size()), zmq::send_flags::none);

    zmq::message_t reply;
    auto result = sock.recv(reply, zmq::recv_flags::none);

    if (result) {
      string reply_data(static_cast<char *>(reply.data()), reply.size());
      Message response = Message::deserialize(reply_data);

      int status = response.payload.value("status", 500);
      if (status == 200) {
        processSyncResponse(response);
      } else {
        Logger::getInstance().warn("NodeServer",
                                  "Sync request to " + node_endpoint +
                                      " returned status " + to_string(status));
      }
    }
  } catch (const exception &e) {
    Logger::getInstance().error("NodeServer",
                               "Failed to request sync from " + node_endpoint +
                                   ": " + string(e.what()));
  }
}

Message NodeServer::handleSyncRequest(const Message &msg) {
  Logger::getInstance().info("NodeServer",
                            "Handling SYNC_REQUEST from " + msg.sender_endpoint);

  string requester_endpoint = msg.sender_endpoint;
  vector<string> all_keys = storage_.getKeys(-1);

  nlohmann::json response_data;
  vector<nlohmann::json> crdts_to_send;

  for (const string &key : all_keys) {
    vector<string> replicas = metadata_.getNodesForKey(key, N);

    // Check if requester is in preference list
    if (contains(replicas, requester_endpoint)) {
      auto crdt = storage_.load(key);
      if (crdt.has_value()) {
        nlohmann::json crdt_item;
        crdt_item["key"] = key;
        crdt_item["data"] = crdt.value();
        crdts_to_send.push_back(crdt_item);
      }
    }
  }

  response_data["crdts"] = crdts_to_send;
  response_data["count"] = crdts_to_send.size();

  Logger::getInstance().info("NodeServer",
                            "Sending " + to_string(crdts_to_send.size()) +
                                " CRDTs to " + requester_endpoint);

  return createResponseMessage(endpoint_, StatusCode::OK, msg.request_id,
                               response_data);
}

void NodeServer::processSyncResponse(const Message &response) {
  if (!response.payload.contains("data") ||
      !response.payload["data"].contains("crdts")) {
    Logger::getInstance().warn("NodeServer",
                              "Sync response missing crdts data");
    return;
  }

  auto crdts = response.payload["data"]["crdts"];
  int merged_count = 0;
  int failed_count = 0;

  for (const auto &item : crdts) {
    try {
      string key = item["key"].get<string>();
      CRDTShoppingList received_crdt = item["data"].get<CRDTShoppingList>();

      auto existing = storage_.load(key);
      if (existing.has_value()) {
        received_crdt.merge(existing.value());
      }

      received_crdt.set_node_id(node_id_);
      bool is_deleted = !received_crdt.hasActiveItems();

      if (storage_.save(key, received_crdt, is_deleted)) {
        merged_count++;
      } else {
        failed_count++;
      }
    } catch (const exception &e) {
      Logger::getInstance().error("NodeServer", "Failed to process CRDT: " +
                                                    string(e.what()));
      failed_count++;
    }
  }

  Logger::getInstance().info("NodeServer",
                            "Sync complete: " + to_string(merged_count) +
                                " keys merged, " + to_string(failed_count) +
                                " failed");
}



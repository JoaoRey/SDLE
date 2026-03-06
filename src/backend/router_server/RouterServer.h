#ifndef ROUTER_SERVER_H
#define ROUTER_SERVER_H

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <zmq.hpp>

#include "../common/Logger.h"
#include "../common/Metadata.h"
#include "../common/Protocol.h"

using namespace std;

class RouterServer {
private:
  string endpoint_;
  int heartbeat_interval_ms_;
  int heartbeat_timeout_ms_;
  int retry_after_ms_;
  int request_timeout_ms_;

  zmq::context_t context_;
  unique_ptr<zmq::socket_t> socket_;

  // Ring state
  Metadata metadata_;

  map<string, chrono::steady_clock::time_point> node_heartbeats_;
  mutex heartbeat_mutex_;

  // Pending client requests
  struct PendingRequest {
    string client_identity;
    chrono::steady_clock::time_point timestamp;
  };
  map<string, PendingRequest> pending_requests_;
  mutex pending_mutex_;

  // endpoint to zmq identity
  map<string, string> node_identities_;
  mutex identity_mutex_;

  atomic<bool> running_{false};
  thread monitor_thread_;

public:
  RouterServer() : context_(1) {}

  ~RouterServer() { stop(); }

  bool initialize(const string &config_path) {
    Logger::getInstance().info("RouterServer", "Router server starting...");

    ifstream config_file(config_path);
    if (!config_file.is_open()) {
      Logger::getInstance().error("RouterServer",
                                  "Failed to open config file: " + config_path);
      return false;
    }

    nlohmann::json config;
    try {
      config_file >> config;
    } catch (const exception &e) {
      Logger::getInstance().error("RouterServer", "Failed to parse config: " +
                                                      string(e.what()));
      return false;
    }

    endpoint_ = config.value("endpoint", "tcp://*:5555");
    heartbeat_interval_ms_ = config.value("heartbeat_interval_ms", 5000);
    heartbeat_timeout_ms_ = config.value("heartbeat_timeout_ms", 15000);
    retry_after_ms_ = config.value("retry_after_ms", 1000);
    request_timeout_ms_ = config.value("request_timeout_ms", 30000);

    socket_ = make_unique<zmq::socket_t>(context_, zmq::socket_type::router);
    socket_->set(zmq::sockopt::linger, 0);
    // Allow routing to disconnected peers
    socket_->set(zmq::sockopt::router_mandatory, 1);

    try {
      socket_->bind(endpoint_);
      Logger::getInstance().ok("RouterServer", "Router bound to " + endpoint_);
    } catch (const zmq::error_t &e) {
      Logger::getInstance().error(
          "RouterServer", "Failed to bind to " + endpoint_ + ": " + e.what());
      return false;
    }

    return true;
  }

  void run() {
    running_ = true;

    monitor_thread_ = thread(&RouterServer::monitorLoop, this);

    Logger::getInstance().info("RouterServer", "Router server running...");

    while (running_) {
      try {
        zmq::pollitem_t items[] = {{*socket_, 0, ZMQ_POLLIN, 0}};

        zmq::poll(items, 1, chrono::milliseconds(100));

        if (items[0].revents & ZMQ_POLLIN) {
          handleIncomingMessage();
        }
      } catch (const zmq::error_t &e) {
        if (running_) {
          Logger::getInstance().error(
              "RouterServer", "ZMQ error in main loop: " + string(e.what()));
        }
      }
    }
    Logger::getInstance().info("RouterServer", "Router server stopping...");
  }

  void stop() {
    if (running_) {
      running_ = false;

      if (monitor_thread_.joinable()) {
        monitor_thread_.join();
      }

      if (socket_) {
        socket_->close();
      }

      Logger::getInstance().info("RouterServer", "Router server stopped");
    }
  }

private:
  void handleIncomingMessage() {
    vector<zmq::message_t> frames;

    while (true) {
      zmq::message_t frame;
      auto result = socket_->recv(frame, zmq::recv_flags::none);
      if (!result)
        break;

      frames.push_back(move(frame));

      int more = socket_->get(zmq::sockopt::rcvmore);
      if (!more)
        break;
    }

    if (frames.size() < 2) {
      Logger::getInstance().error("RouterServer",
                                  "Invalid message: too few frames (" +
                                      to_string(frames.size()) + ")");
      return;
    }

    // identity
    string identity(static_cast<char *>(frames[0].data()), frames[0].size());
    Logger::getInstance().info(
        "RouterServer",
        "Received message from: " + (identity.empty() ? "EMPTY_ID" : identity) +
            " with " + to_string(frames.size()) + " frames");

    // data frame
    string data;
    for (size_t i = frames.size() - 1; i >= 1; --i) {
      if (frames[i].size() > 0) {
        data = string(static_cast<char *>(frames[i].data()), frames[i].size());
        break;
      }
    }

    if (data.empty()) {
      Logger::getInstance().error("RouterServer",
                                  "Invalid message: no data frame");
      return;
    }

    try {
      Message msg = Message::deserialize(data);
      processMessage(identity, msg);
    } catch (const exception &e) {
      Logger::getInstance().error("RouterServer", "Error processing message: " +
                                                      string(e.what()));
    }
  }

  void processMessage(const string &identity, const Message &msg) {
    switch (msg.type) {
    case MessageType::REGISTER:
      handleRegister(identity, msg);
      break;
    case MessageType::UNREGISTER:
      handleUnregister(identity, msg);
      break;
    case MessageType::HEARTBEAT:
      handleHeartbeat(identity, msg);
      break;
    case MessageType::GET:
    case MessageType::PUT:
    case MessageType::DELETE:
      handleClientRequest(identity, msg);
      break;
    case MessageType::LIST_NODES:
      handleListNodes(identity, msg);
      break;
    case MessageType::STOP_NODE:
      handleStopNode(identity, msg);
      break;
    case MessageType::PING:
      handlePing(identity, msg);
      break;
    case MessageType::RESPONSE:
      handleResponse(identity, msg);
      break;
    default:
      Logger::getInstance().warn("RouterServer",
                                 "Unknown message type " +
                                     to_string(static_cast<int>(msg.type)) +
                                     " from " + msg.sender_endpoint);
      break;
    }
  }

  void handleRegister(const string &identity, const Message &msg) {
    string endpoint = msg.sender_endpoint;
    vector<VNode> vnodes = msg.payload.at("vnodes").get<vector<VNode>>();

    Logger::getInstance().info("RouterServer",
                               "Registering node: " + endpoint + " with " +
                                   to_string(vnodes.size()) + " vnodes");

    {
      lock_guard<mutex> lock(identity_mutex_);
      node_identities_[endpoint] = identity;
    } // Release lock

    metadata_.addNode(endpoint, vnodes);

    {
      lock_guard<mutex> lock(heartbeat_mutex_);
      node_heartbeats_[endpoint] = chrono::steady_clock::now();
    } // Release lock

    Message ack = createRegisterAckMessage(endpoint_, metadata_.toJson());
    sendToNode(identity, ack);

    // Broadcast to all
    broadcastSyncMetadata(endpoint);

    Logger::getInstance().info("RouterServer",
                               "Node registered. Total nodes: " +
                                   to_string(metadata_.nodeCount()));
  }

  void handleUnregister(const string &identity, const Message &msg) {
    string endpoint = msg.sender_endpoint;

    Logger::getInstance().info("RouterServer",
                               "Unregistering node: " + endpoint);

    removeNode(endpoint);
    broadcastSyncMetadata("");

    Logger::getInstance().info("RouterServer",
                               "Node unregistered. Total nodes: " +
                                   to_string(metadata_.nodeCount()));
  }

  void handleHeartbeat(const string &identity, const Message &msg) {
    string endpoint = msg.sender_endpoint;

    lock_guard<mutex> lock(heartbeat_mutex_);
    if (node_heartbeats_.find(endpoint) != node_heartbeats_.end()) {
      node_heartbeats_[endpoint] = chrono::steady_clock::now();
      // Logger::getInstance().debug("RouterServer", "Heartbeat from " +
      // endpoint);
    } else {
      Logger::getInstance().warn("RouterServer",
                                 "Heartbeat from unknown node: " + endpoint);
    }
  }

  void handleClientRequest(const string &identity, const Message &msg) {
    string list_name = msg.payload.value("list_name", "");
    Logger::getInstance().info(
        "RouterServer",
        "Handling client request: " + messageTypeToString(msg.type) +
            " for list: " + list_name);

    if (list_name.empty()) {
      // Send error response
      Message response = createResponseMessage(
          endpoint_, StatusCode::INTERNAL_ERROR, msg.request_id);
      sendToClient(identity, response);
      return;
    }

    // Find coordinator node
    vector<string> coordinators = metadata_.getNodesForKey(list_name, 1);
    if (coordinators.empty()) {
      Logger::getInstance().warn("RouterServer",
                                 "No coordinator found for key: " + list_name);
      Message response =
          createResponseMessage(endpoint_, StatusCode::UNAVAILABLE,
                                msg.request_id, nullptr, retry_after_ms_);
      sendToClient(identity, response);
      return;
    }
    // First on the ring
    string coordinator = coordinators[0];
    Logger::getInstance().info(
        "RouterServer", "Key " + list_name + " mapped to node " + coordinator);

    // Get coordinator's identity
    string coord_identity;
    {
      lock_guard<mutex> lock(identity_mutex_);
      auto it = node_identities_.find(coordinator);
      if (it == node_identities_.end()) {
        Logger::getInstance().error("RouterServer",
                                    "Coordinator identity not found for " +
                                        coordinator);
        // Coordinator not found
        Message response =
            createResponseMessage(endpoint_, StatusCode::UNAVAILABLE,
                                  msg.request_id, nullptr, retry_after_ms_);
        sendToClient(identity, response);
        return;
      }
      coord_identity = it->second;
    }

    // Store pending request
    {
      lock_guard<mutex> lock(pending_mutex_);
      pending_requests_[msg.request_id] = {identity,
                                           chrono::steady_clock::now()};
    }
    sendToNode(coord_identity, msg);
  }

  void handleResponse(const string &identity, const Message &msg) {
    string request_id = msg.request_id;
    Logger::getInstance().info("RouterServer",
                               "Handling RESPONSE for " + request_id);

    // Find pending request
    string client_identity;
    {
      lock_guard<mutex> lock(pending_mutex_);
      auto it = pending_requests_.find(request_id);
      if (it == pending_requests_.end()) {
        Logger::getInstance().warn("RouterServer",
                                   "Request " + request_id +
                                       " not found (timed out?)");
        return;
      }
      client_identity = it->second.client_identity;
      pending_requests_.erase(it);
    }

    sendToClient(client_identity, msg);
  }

  void handleListNodes(const string &identity, const Message &msg) {
    Logger::getInstance().info("RouterServer",
                               "Handling LIST_NODES request from " + identity);
    vector<string> nodes = metadata_.getAllEndpoints();
    nlohmann::json payload;
    payload["nodes"] = nodes;

    Message response = createResponseMessage(endpoint_, StatusCode::OK,
                                             msg.request_id, payload);
    sendToClient(identity, response);
  }

  void handleStopNode(const string &identity, const Message &msg) {
    string target_endpoint = msg.payload.value("target_endpoint", "");
    Logger::getInstance().info(
        "RouterServer", "Handling STOP_NODE request for " + target_endpoint);

    string target_identity;
    {
      lock_guard<mutex> lock(identity_mutex_);
      auto it = node_identities_.find(target_endpoint);
      if (it != node_identities_.end()) {
        target_identity = it->second;
      }
    }

    if (!target_identity.empty()) {
      // Forward the STOP_NODE message to the node
      sendToNode(target_identity, msg);

      // Send success back to admin
      Message response =
          createResponseMessage(endpoint_, StatusCode::OK, msg.request_id);
      sendToClient(identity, response);
    } else {
      Logger::getInstance().error("RouterServer",
                                  "Target node identity not found for " +
                                      target_endpoint);
      // Node not found
      Message response = createResponseMessage(endpoint_, StatusCode::NOT_FOUND,
                                               msg.request_id);
      sendToClient(identity, response);
    }
  }

  void handlePing(const string &identity, const Message &msg) {
    Logger::getInstance().info("RouterServer",
                               "Handling PING request from " + identity);
    // Just reply with OK
    Message response =
        createResponseMessage(endpoint_, StatusCode::OK, msg.request_id);
    sendToClient(identity, response);
  }

  void removeNode(const string &endpoint) {
    metadata_.removeNode(endpoint);
    Logger::getInstance().info("RouterServer", "Node removed: " + endpoint);

    {
      lock_guard<mutex> lock(heartbeat_mutex_);
      node_heartbeats_.erase(endpoint);
    }

    {
      lock_guard<mutex> lock(identity_mutex_);
      node_identities_.erase(endpoint);
    }
  }

  void broadcastSyncMetadata(const string &exclude_endpoint) {
    Message sync = createSyncMetadataMessage(endpoint_, metadata_.toJson());
    string data = sync.serialize();

    vector<pair<string, string>> nodes_to_send;
    {
      lock_guard<mutex> lock(identity_mutex_);
      for (const auto &[endpoint, identity] : node_identities_) {
        if (endpoint != exclude_endpoint) {
          nodes_to_send.push_back({endpoint, identity});
        }
      }
    }

    for (const auto &[endpoint, identity] : nodes_to_send) {
      try {
        zmq::message_t id_frame(identity.data(), identity.size());
        zmq::message_t data_frame(data.data(), data.size());

        socket_->send(id_frame, zmq::send_flags::sndmore);
        socket_->send(data_frame, zmq::send_flags::none);
      } catch (const zmq::error_t &e) {
        cerr << "Failed to send SYNC_METADATA to " << endpoint << ": "
             << e.what() << endl;
      }
    }
  }

  /// identity+data
  void sendToNode(const string &identity, const Message &msg) {
    try {
      string data = msg.serialize();

      zmq::message_t id_frame(identity.data(), identity.size());
      zmq::message_t data_frame(data.data(), data.size());

      socket_->send(id_frame, zmq::send_flags::sndmore);
      socket_->send(data_frame, zmq::send_flags::none);
    } catch (const zmq::error_t &e) {
      cerr << "Failed to send message to node: " << e.what() << endl;
    }
  }

  /// identity+data
  /// identity+empty+data
  void sendToClient(const string &identity, const Message &msg) {
    Logger::getInstance().info("RouterServer",
                               "Sending response to client " + identity);
    try {
      string data = msg.serialize();

      zmq::message_t id_frame(identity.data(), identity.size());
      zmq::message_t empty_frame;
      zmq::message_t data_frame(data.data(), data.size());

      socket_->send(id_frame, zmq::send_flags::sndmore);
      socket_->send(empty_frame, zmq::send_flags::sndmore);
      socket_->send(data_frame, zmq::send_flags::none);
      Logger::getInstance().info("RouterServer", "Sent to client");
    } catch (const zmq::error_t &e) {
      Logger::getInstance().error("RouterServer",
                                  "Failed to send message to client: " +
                                      string(e.what()));
    }
  }

  void monitorLoop() {
    while (running_) {
      this_thread::sleep_for(chrono::milliseconds(heartbeat_interval_ms_));

      if (!running_)
        break;

      checkNodeHeartbeats();
      checkStaleRequests();
    }
  }

  void checkNodeHeartbeats() {
    auto now = chrono::steady_clock::now();
    vector<string> dead_nodes;

    {
      lock_guard<mutex> lock(heartbeat_mutex_);
      for (const auto &[endpoint, last_heartbeat] : node_heartbeats_) {
        auto elapsed =
            chrono::duration_cast<chrono::milliseconds>(now - last_heartbeat)
                .count();
        if (elapsed > heartbeat_timeout_ms_) {
          dead_nodes.push_back(endpoint);
        }
      }
    }

    if (!dead_nodes.empty()) {
      for (const string &endpoint : dead_nodes) {
        Logger::getInstance().info("RouterServer",
                                   "Node " + endpoint +
                                       " timed out, removing from ring");
        removeNode(endpoint);
      }
      broadcastSyncMetadata("");
      Logger::getInstance().info(
          "RouterServer",
          "Removed " + to_string(dead_nodes.size()) +
              " dead nodes. Total nodes: " + to_string(metadata_.nodeCount()));
    }
  }

  void checkStaleRequests() {
    auto now = chrono::steady_clock::now();
    vector<pair<string, string>> stale_requests;

    {
      lock_guard<mutex> lock(pending_mutex_);
      for (auto it = pending_requests_.begin();
           it != pending_requests_.end();) {
        auto elapsed = chrono::duration_cast<chrono::milliseconds>(
                           now - it->second.timestamp)
                           .count();
        if (elapsed > request_timeout_ms_) {
          stale_requests.push_back({it->first, it->second.client_identity});
          it = pending_requests_.erase(it);
        } else {
          ++it;
        }
      }
    }

    for (const auto &[request_id, client_identity] : stale_requests) {
      Message response =
          createResponseMessage(endpoint_, StatusCode::UNAVAILABLE, request_id,
                                nullptr, retry_after_ms_);
      sendToClient(client_identity, response);
      Logger::getInstance().info("RouterServer",
                                 "Request " + request_id +
                                     " timed out, sent UNAVAILABLE to client");
    }
  }
};

#endif // ROUTER_SERVER_H
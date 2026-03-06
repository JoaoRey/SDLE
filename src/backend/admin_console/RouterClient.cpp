#include "RouterClient.h"
#include "../common/Logger.h"
#include <chrono>
#include <unistd.h>

using namespace std;

RouterClient::RouterClient(zmq::context_t &context)
    : socket_(context, zmq::socket_type::dealer), context_(context),
      connected_(false) {

  string identity = "admin_console_" + to_string(getpid());
  socket_.set(zmq::sockopt::routing_id, identity);
  socket_.set(zmq::sockopt::linger, 0);
}

RouterClient::~RouterClient() { socket_.close(); }

bool RouterClient::connect(const string &endpoint) {
  router_endpoint_ = endpoint;
  try {
    socket_.connect(endpoint);
    connected_ = true;
    return true;
  } catch (const zmq::error_t &e) {
    Logger::getInstance().error("RouterClient",
                                "Failed to connect: " + string(e.what()));
    connected_ = false;
    return false;
  }
}

bool RouterClient::isConnected() const { return connected_; }

void RouterClient::sendRequest(const Message &msg) {
  string data = msg.serialize();
  socket_.send(zmq::message_t(), zmq::send_flags::sndmore);
  socket_.send(zmq::message_t(data.data(), data.size()), zmq::send_flags::none);
}

std::optional<Message> RouterClient::receiveResponse(int timeout_ms) {
  zmq::pollitem_t items[] = {{socket_, 0, ZMQ_POLLIN, 0}};
  zmq::poll(items, 1, chrono::milliseconds(timeout_ms));

  if (items[0].revents & ZMQ_POLLIN) {
    zmq::message_t empty, content;
    (void)socket_.recv(empty, zmq::recv_flags::none);
    (void)socket_.recv(content, zmq::recv_flags::none);

    string data(static_cast<char *>(content.data()), content.size());
    return Message::deserialize(data);
  }

  return std::nullopt;
}

bool RouterClient::ping() {
  Logger::getInstance().info("RouterClient", "Pinging router...");
  Message msg = createPingMessage("admin_console");
  sendRequest(msg);

  try {
    auto responseOpt = receiveResponse();
    if (responseOpt) {
      if (responseOpt->payload["status"] == 200) {
        Logger::getInstance().ok("RouterClient", "Router is ONLINE.");
        return true;
      }
    } else {
      Logger::getInstance().error("RouterClient", "Ping timed out.");
    }
  } catch (const exception &e) {
    Logger::getInstance().error("RouterClient",
                                "Ping failed: " + string(e.what()));
  }
  return false;
}

vector<string> RouterClient::getNodes() {
  Logger::getInstance().info("RouterClient", "Requesting node list...");
  Message msg = createListNodesMessage("admin_console");
  sendRequest(msg);

  try {
    auto responseOpt = receiveResponse();
    if (responseOpt) {
      if (responseOpt->payload["status"] == 200) {
        if (!responseOpt->payload["data"]["nodes"].is_null()) {
          return responseOpt->payload["data"]["nodes"].get<vector<string>>();
        } else {
          return {};
        }
      } else {
        Logger::getInstance().error("RouterClient",
                                    "Failed to get nodes: " +
                                        responseOpt->payload.dump());
      }
    } else {
      Logger::getInstance().error("RouterClient", "Get nodes timed out.");
    }
  } catch (const exception &e) {
    Logger::getInstance().error("RouterClient",
                                "Error getting nodes: " + string(e.what()));
  }
  return {};
}

bool RouterClient::stopNode(const string &endpoint) {
  Logger::getInstance().info("RouterClient",
                             "Requesting stop for node: " + endpoint);
  Message msg = createStopNodeMessage("admin_console", endpoint);
  sendRequest(msg);

  try {
    auto responseOpt = receiveResponse();
    if (responseOpt) {
      if (responseOpt->payload["status"] == 200) {
        Logger::getInstance().ok("RouterClient", "Node stopped successfully.");
        return true;
      } else {
        Logger::getInstance().error("RouterClient",
                                    "Failed to stop node: " +
                                        responseOpt->payload.dump());
      }
    } else {
      Logger::getInstance().error("RouterClient", "Stop node timed out.");
    }
  } catch (const exception &e) {
    Logger::getInstance().error("RouterClient",
                                "Error stopping node: " + string(e.what()));
  }
  return false;
}

std::optional<Message>
RouterClient::sendDirectRequest(const std::string &node_endpoint,
                                const Message &msg, int timeout_ms) {
  try {
    zmq::socket_t sock(context_, zmq::socket_type::req);
    sock.set(zmq::sockopt::linger, 0);
    sock.set(zmq::sockopt::rcvtimeo, timeout_ms);
    sock.connect(node_endpoint);

    string data = msg.serialize();
    sock.send(zmq::message_t(data.data(), data.size()), zmq::send_flags::none);

    zmq::message_t reply;
    auto result = sock.recv(reply, zmq::recv_flags::none);

    if (result) {
      string reply_data(static_cast<char *>(reply.data()), reply.size());
      return Message::deserialize(reply_data);
    }
  } catch (const exception &e) {
    Logger::getInstance().error("RouterClient",
                                "Direct request failed: " + string(e.what()));
  }
  return std::nullopt;
}

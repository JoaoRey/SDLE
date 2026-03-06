#ifndef ROUTER_CLIENT_H
#define ROUTER_CLIENT_H

#include "../common/Protocol.h"
#include <optional>
#include <string>
#include <vector>
#include <zmq.hpp>

class RouterClient {
public:
  RouterClient(zmq::context_t &context);
  ~RouterClient();

  bool connect(const std::string &endpoint);
  bool isConnected() const;

  // Commands
  bool ping();
  std::vector<std::string> getNodes();
  bool stopNode(const std::string &endpoint);

  // Direct Node Communication (Debug)
  std::optional<Message> sendDirectRequest(const std::string &node_endpoint,
                                           const Message &msg,
                                           int timeout_ms = 2000);

private:
  zmq::socket_t socket_;
  zmq::context_t &context_;
  std::string router_endpoint_;
  bool connected_;

  void sendRequest(const Message &msg);
  std::optional<Message> receiveResponse(int timeout_ms = 2000);
};

#endif // ROUTER_CLIENT_H

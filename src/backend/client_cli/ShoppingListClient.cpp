#include "ShoppingListClient.h"
#include "../common/Logger.h"
#include <chrono>
#include <iostream>

using namespace std;

ShoppingListClient::ShoppingListClient(zmq::context_t &context)
    : socket_(context, zmq::socket_type::dealer) {
  // Default minimal ID
  client_id_ =
      "client_" +
      to_string(chrono::system_clock::now().time_since_epoch().count());
  socket_.set(zmq::sockopt::routing_id, client_id_);
}

ShoppingListClient::~ShoppingListClient() { socket_.close(); }

void ShoppingListClient::setClientId(const string &id) {
  client_id_ = id;
  socket_.set(zmq::sockopt::routing_id, client_id_);
}

bool ShoppingListClient::connect(const string &endpoint) {
  try {
    router_endpoint_ = endpoint;
    socket_.connect(router_endpoint_);
    return true;
  } catch (const exception &e) {
    cerr << "Failed to connect: " << e.what() << endl;
    return false;
  }
}

optional<CRDTShoppingList>
ShoppingListClient::getList(const string &list_name) {
  nlohmann::json payload;
  payload["list_name"] = list_name;

  nlohmann::json response = sendRequest("GET", payload);

  if (response.is_null()) {
    Logger::getInstance().error("Client", "GET request timed out");
    return nullopt;
  }

  int status = response["payload"].value("status", 500);
  if (status == 200 && response["payload"].contains("data")) {
    try {
      return response["payload"]["data"].get<CRDTShoppingList>();
    } catch (const exception &e) {
      Logger::getInstance().error("Client", "Failed to deserialize list: " +
                                                string(e.what()));
    }
  }

  return nullopt;
}

bool ShoppingListClient::putList(const string &list_name,
                                 const CRDTShoppingList &list) {
  nlohmann::json payload;
  payload["list_name"] = list_name;
  payload["data"] = list;

  nlohmann::json response = sendRequest("PUT", payload);

  if (response.is_null())
    return false;

  int status = response["payload"].value("status", 500);
  return status == 200;
}

bool ShoppingListClient::deleteList(const string &list_name) {
  nlohmann::json payload;
  payload["list_name"] = list_name;

  nlohmann::json response = sendRequest("DELETE", payload);

  if (response.is_null())
    return false;

  int status = response["payload"].value("status", 500);
  return status == 200;
}

nlohmann::json ShoppingListClient::sendRequest(const string &type,
                                               const nlohmann::json &payload) {
  string req_id =
      "req_" +
      to_string(chrono::steady_clock::now().time_since_epoch().count());

  nlohmann::json msg_json;
  msg_json["type"] = type;
  msg_json["sender_endpoint"] = client_id_;
  msg_json["request_id"] = req_id;
  msg_json["payload"] = payload;

  string data = msg_json.dump();

  // Dealer pattern: empty frame + content
  try {
    socket_.send(zmq::message_t(), zmq::send_flags::sndmore);
    socket_.send(zmq::message_t(data.data(), data.size()),
                 zmq::send_flags::none);
  } catch (const exception &e) {
    Logger::getInstance().error("Client",
                                "Failed to send request: " + string(e.what()));
    return nullptr;
  }

  // Poll for response (timeout 3s)
  zmq::pollitem_t items[] = {{socket_, 0, ZMQ_POLLIN, 0}};
  zmq::poll(items, 1, chrono::milliseconds(3000));

  if (items[0].revents & ZMQ_POLLIN) {
    try {
      zmq::message_t empty;
      zmq::message_t content;
      (void)socket_.recv(empty, zmq::recv_flags::none);
      auto res = socket_.recv(content, zmq::recv_flags::none);

      if (res) {
        string reply_str(static_cast<char *>(content.data()), content.size());
        return nlohmann::json::parse(reply_str);
      }
    } catch (const exception &e) {
      Logger::getInstance().error("Client", "Failed to receive response: " +
                                                string(e.what()));
    }
  }

  return nullptr;
}

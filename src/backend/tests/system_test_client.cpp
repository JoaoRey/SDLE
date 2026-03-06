#include "../common/Protocol.h"
#include "../crdt/CRDTShoppingList.h"
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>
#include <zmq.hpp>

using namespace std;

// Helper to send request and wait for response
nlohmann::json sendRequest(zmq::socket_t &socket, const string &type,
                           const nlohmann::json &payload) {
  string req_id =
      "req_" +
      to_string(chrono::system_clock::now().time_since_epoch().count());

  nlohmann::json msg_json;
  msg_json["type"] = type;
  msg_json["sender_endpoint"] = "client_test";
  msg_json["request_id"] = req_id;
  msg_json["payload"] = payload;

  string data = msg_json.dump();

  // Dealer: empty frame + data
  socket.send(zmq::message_t(), zmq::send_flags::sndmore);
  socket.send(zmq::message_t(data.data(), data.size()), zmq::send_flags::none);

  // Wait for response
  zmq::pollitem_t items[] = {{socket, 0, ZMQ_POLLIN, 0}};
  zmq::poll(items, 1, chrono::milliseconds(2000));

  if (items[0].revents & ZMQ_POLLIN) {
    zmq::message_t empty;
    zmq::message_t content;
    (void)socket.recv(empty, zmq::recv_flags::none);
    (void)socket.recv(content, zmq::recv_flags::none);

    string reply_str(static_cast<char *>(content.data()), content.size());
    return nlohmann::json::parse(reply_str);
  }

  return nullptr;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    cerr << "Usage: " << argv[0] << " <PUT|GET>" << endl;
    return 1;
  }
  string mode = argv[1];

  zmq::context_t context(1);
  zmq::socket_t socket(context, zmq::socket_type::dealer);

  socket.set(zmq::sockopt::routing_id, "client_test");
  socket.connect("tcp://127.0.0.1:5555");

  cout << "Connected to router..." << endl;
  this_thread::sleep_for(chrono::seconds(1));

  // Test Data
  string list_name = "groceries";
  CRDTShoppingList list("client_test", list_name);
  list.add_item("milk", "t1");

  nlohmann::json list_json = list;

  if (mode == "PUT") {
    // 1. PUT
    cout << "Sending PUT..." << endl;
    nlohmann::json put_payload;
    put_payload["list_name"] = list_name;
    put_payload["data"] = list_json;

    auto put_resp = sendRequest(socket, "PUT", put_payload);
    if (put_resp.is_null() || put_resp["payload"]["status"] != 200) {
      cerr << "PUT Failed: "
           << (put_resp.is_null() ? "Timeout" : put_resp.dump()) << endl;
      return 1;
    }
    cout << "PUT Success" << endl;
  } else if (mode == "GET") {
    // 2. GET
    cout << "Sending GET..." << endl;
    nlohmann::json get_payload;
    get_payload["list_name"] = list_name;

    auto get_resp = sendRequest(socket, "GET", get_payload);
    if (get_resp.is_null() || get_resp["payload"]["status"] != 200) {
      cerr << "GET Failed: "
           << (get_resp.is_null() ? "Timeout" : get_resp.dump()) << endl;
      return 1;
    }

    // Verify Data
    CRDTShoppingList received_list =
        get_resp["payload"]["data"].get<CRDTShoppingList>();
    if (!received_list.contains("milk")) {
      cerr << "GET Verification Failed: 'milk' missing" << endl;
      return 1;
    }

    cout << "GET Success: Found 'milk'" << endl;
  } else {
    cerr << "Unknown mode: " << mode << endl;
    return 1;
  }

  return 0;
}

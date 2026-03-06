#ifndef SHOPPING_LIST_CLIENT_H
#define SHOPPING_LIST_CLIENT_H

#include "../crdt/CRDTShoppingList.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <zmq.hpp>

class ShoppingListClient {
public:
  explicit ShoppingListClient(zmq::context_t &context);
  ~ShoppingListClient();

  bool connect(const std::string &endpoint);
  void setClientId(const std::string &id);
  std::string getClientId() const { return client_id_; }

  std::optional<CRDTShoppingList> getList(const std::string &list_name);
  bool putList(const std::string &list_name, const CRDTShoppingList &list);
  bool deleteList(const std::string &list_name);

private:
  zmq::socket_t socket_;
  std::string client_id_;
  std::string router_endpoint_;

  nlohmann::json sendRequest(const std::string &type,
                             const nlohmann::json &payload);
};

#endif // SHOPPING_LIST_CLIENT_H

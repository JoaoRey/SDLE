#ifndef CLIENT_CLI_H
#define CLIENT_CLI_H

#include "ShoppingListClient.h"
#include <optional>
#include <string>

class ClientCLI {
public:
  ClientCLI();
  ~ClientCLI();
  void run();

private:
  zmq::context_t context_;
  ShoppingListClient client_;

  std::string current_list_name_;
  std::optional<CRDTShoppingList> current_list_;

  bool running_;

  void processCommand(const std::string &line);
  void printHelp();
  void showList();
  void printStatus(const std::string &msg, bool success = true);

  // Operations
  void doOpen(const std::string &name);
  void doCreate(const std::string &name);
  void doAdd(const std::string &item, int qty);
  void doCheck(const std::string &item);
  void doUncheck(const std::string &item);
  void doDelete(const std::string &item);
  void doSync();
  void doRemoveList();

  // Helper to sync state
  void syncAndPush();
  void performSync();
};

#endif // CLIENT_CLI_H

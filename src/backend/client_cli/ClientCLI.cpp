#include "ClientCLI.h"
#include <iostream>
#include <sstream>

using namespace std;

ClientCLI::ClientCLI() : context_(1), client_(context_), running_(true) {
  // Use a random client id
  srand(time(0));
  string id = "cli_client_" + to_string(rand() % 10000);
  client_.setClientId(id);

  // Connect to default router
  // Ideally this is configurable, but hardcoding for simple CLI
  string router = "tcp://127.0.0.1:5555";
  if (!client_.connect(router)) {
    cerr << "Warning: Could not connect to router at " << router << endl;
  } else {
    cout << "Connected to router at " << router << endl;
  }
}

ClientCLI::~ClientCLI() {}

void ClientCLI::run() {
  cout << "========================================" << endl;
  cout << "   Shopping List Client CLI             " << endl;
  cout << "========================================" << endl;
  printHelp();

  string line;
  while (running_) {
    if (current_list_.has_value()) {
      cout << "[" << current_list_name_ << "] > ";
    } else {
      cout << "(no list) > ";
    }

    if (!getline(cin, line))
      break;
    if (line.empty())
      continue;

    processCommand(line);
  }
}

void ClientCLI::processCommand(const string &line) {
  stringstream ss(line);
  string cmd;
  ss >> cmd;

  if (cmd == "help") {
    printHelp();
  } else if (cmd == "exit") {
    running_ = false;
  } else if (cmd == "create") {
    string name;
    ss >> name;
    if (name.empty()) {
      cout << "Usage: create <list_name>" << endl;
    } else {
      doCreate(name);
    }
  } else if (cmd == "open") {
    string name;
    ss >> name;
    if (name.empty()) {
      cout << "Usage: open <list_name>" << endl;
    } else {
      doOpen(name);
    }
  } else if (cmd == "show") {
    showList();
  } else if (cmd == "sync") {
    if (!current_list_.has_value()) {
      cout << "No list open." << endl;
    } else {
      doSync();
    }
  } else if (cmd == "delete_list") {
    doRemoveList();
  } else if (cmd == "clear") {
    system("clear");
  }
  // Item operations
  else if (cmd == "add") {
    string item;
    int qty = 1;
    ss >> item;
    if (item.empty()) {
      cout << "Usage: add <item> [qty]" << endl;
    } else {
      if (!(ss >> qty))
        qty = 1;
      doAdd(item, qty);
    }
  } else if (cmd == "del" || cmd == "rm") {
    string item;
    ss >> item;
    if (item.empty()) {
      cout << "Usage: del <item>" << endl;
    } else {
      doDelete(item);
    }
  } else if (cmd == "check") {
    string item;
    ss >> item;
    if (item.empty()) {
      cout << "Usage: check <item>" << endl;
    } else {
      doCheck(item);
    }
  } else if (cmd == "uncheck") {
    string item;
    ss >> item;
    if (item.empty()) {
      cout << "Usage: uncheck <item>" << endl;
    } else {
      doUncheck(item);
    }
  } else {
    cout << "Unknown command. Type 'help'." << endl;
  }
}

void ClientCLI::printHelp() {
  cout << "Commands:" << endl;
  cout << "  create <name>      - Create and open a new list" << endl;
  cout << "  open <name>        - Open an existing list (or create local if "
          "missing)"
       << endl;
  cout << "  show               - Show current list contents" << endl;
  cout << "  add <item> [qty]   - Add item or increase quantity" << endl;
  cout << "  del <item>         - Remove item" << endl;
  cout << "  check <item>       - Mark item as checked" << endl;
  cout << "  uncheck <item>     - Mark item as unchecked" << endl;
  cout << "  delete_list        - Permanently delete current list from server"
       << endl;
  cout << "  sync               - Force sync with server" << endl;
  cout << "  clear              - Clear screen" << endl;
  cout << "  exit               - Exit" << endl;
}

void ClientCLI::doCreate(const string &name) {
  // TODO: Generate a unique replica_id
  current_list_name_ = name;
  current_list_ = CRDTShoppingList(client_.getClientId(), name);

  doSync();
}

void ClientCLI::doOpen(const string &name) {
  current_list_name_ = name;
  cout << "Opening '" << name << "'..." << endl;

  // Try to load from server
  auto remote = client_.getList(name);
  if (remote.has_value()) {
    current_list_ = remote.value();
    cout << "Loaded list from server." << endl;
  } else {
    // If not found, create new locally but warn.
    cout << "List not found on server. Creating new local instance." << endl;
    current_list_ = CRDTShoppingList(client_.getClientId(), name);
  }
  showList();
}

void ClientCLI::doSync() {
  if (!current_list_.has_value())
    return;

  cout << "Syncing..." << endl;
  performSync();

  // 3. Put merged back (Push)
  if (client_.putList(current_list_name_, current_list_.value())) {
    cout << "Saved to server." << endl;
  } else {
    cout << "Failed to save to server (Offline?)" << endl;
  }
}

void ClientCLI::performSync() {
  if (!current_list_.has_value())
    return;

  // 1. Get remote
  auto remote = client_.getList(current_list_name_);
  if (remote.has_value()) {
    // 2. Merge remote into local
    current_list_->merge(remote.value());
    // Silent success
  } else {
    // Silent failure
  }
}

void ClientCLI::syncAndPush() {
  if (!current_list_.has_value())
    return;

  // Since we merged in performSync (called before ops), we just Push here.
  if (client_.putList(current_list_name_, current_list_.value())) {
    // Success
  } else {
    cout << "(Pending sync - Saved locally)" << endl;
  }
}

void ClientCLI::doAdd(const string &item, int qty) {
  if (!current_list_.has_value()) {
    cout << "No list open." << endl;
    return;
  }

  // Sync before write
  performSync();

  current_list_->add_item(item, to_string(rand()));
  if (qty > 1) {
    current_list_->increment(item, qty);
  } else {
    current_list_->increment(item, 1);
  }

  cout << "Added " << qty << " x " << item << endl;
  syncAndPush();
}

void ClientCLI::doCheck(const string &item) {
  if (!current_list_.has_value())
    return;

  performSync();

  current_list_->check(item, "chk_" + to_string(rand()));
  cout << "Checked " << item << endl;
  syncAndPush();
}

void ClientCLI::doUncheck(const string &item) {
  if (!current_list_.has_value())
    return;

  performSync();

  current_list_->uncheck(item);
  cout << "Unchecked " << item << endl;
  syncAndPush();
}

void ClientCLI::doDelete(const string &item) {
  if (!current_list_.has_value())
    return;

  performSync();

  current_list_->remove_item(item);
  cout << "Removed " << item << endl;
  syncAndPush();
}

void ClientCLI::doRemoveList() {
  if (!current_list_.has_value())
    return;

  if (client_.deleteList(current_list_name_)) {
    cout << "List deleted from server." << endl;
    current_list_.reset();
    current_list_name_ = "";
  } else {
    cout << "Failed to delete list." << endl;
  }
}

void ClientCLI::showList() {
  if (!current_list_.has_value()) {
    cout << "No list open." << endl;
    return;
  }

  // Sync before read
  performSync();

  cout << "--- List: " << current_list_name_ << " ---" << endl;
  auto items = current_list_->get_item_names();
  if (items.empty()) {
    cout << "(empty)" << endl;
  }

  for (const auto &item : items) {
    int qty = current_list_->get_quantity(item);
    bool chk = current_list_->is_checked(item);

    cout << (chk ? "[x] " : "[ ] ") << item << " (x" << qty << ")" << endl;
  }
  cout << "-----------------------" << endl;
}

#ifndef CRDT_CRDTSHOPPINGLIST_H
#define CRDT_CRDTSHOPPINGLIST_H

#include "CRDTCounter.h"
#include "CRDTFlag.h"
#include "CRDTSet.h"
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace std;

class CRDTShoppingList {
private:
  string node_id;
  string list_name;

  // Membership by item name
  CRDTSet<string, string> items;

  unordered_map<string, CRDTCounter<string, int>> quantities;
  unordered_map<string, CRDTFlag<string>> checks;

  // Ensure auxiliary CRDTs exist for an item
  void ensure_item_structs_(const string &item);

public:
  CRDTShoppingList() : node_id(), list_name(), items() {}
  explicit CRDTShoppingList(string replica_id, string name);

  // Getters
  const string &name() const { return list_name; }
  const string &id() const { return node_id; }

  const CRDTSet<string, string> &get_items() const { return items; }
  const unordered_map<string, CRDTCounter<string, int>> &
  get_quantities() const {
    return quantities;
  }
  const unordered_map<string, CRDTFlag<string>> &get_checks() const {
    return checks;
  }

  void set_node_id(const string &id) {
    node_id = id;
    items.set_node_id(id);
    for (auto &[key, counter] : quantities) {
      counter.set_node_id(id);
    }
    for (auto &[key, flag] : checks) {
      flag.set_node_id(id);
    }
  }
  void set_list_name(const string &name) { list_name = name; }
  void set_items(const CRDTSet<string, string> &i) { items = i; }
  void
  set_quantities(const unordered_map<string, CRDTCounter<string, int>> &q) {
    quantities = q;
  }
  void set_checks(const unordered_map<string, CRDTFlag<string>> &c) {
    checks = c;
  }

  void add_item(const string &item, const string &tag);

  void remove_item(const string &item);

  void increment(const string &item, int amount = 1);

  void decrement(const string &item, int amount = 1);

  int get_quantity(const string &item) const;

  bool contains(const string &item) const;

  unordered_set<string> get_item_names() const;

  void check(const string &item, const string &tag);

  void uncheck(const string &item);

  bool is_checked(const string &item) const;

  // Check if any item has non-tombstoned tags (list is not fully deleted)
  bool hasActiveItems() const;

  void merge(const CRDTShoppingList &other);
};

void to_json(nlohmann::json &j, const CRDTShoppingList &list);

void from_json(const nlohmann::json &j, CRDTShoppingList &list);

#endif // CRDT_CRDTSHOPPINGLIST_H

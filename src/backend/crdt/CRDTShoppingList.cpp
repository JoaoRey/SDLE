#include "CRDTShoppingList.h"

using namespace std;

CRDTShoppingList::CRDTShoppingList(string replica_id, string name)
    : node_id(std::move(replica_id)), list_name(std::move(name)),
      items(node_id) {}

void CRDTShoppingList::ensure_item_structs_(const string &item) {
  if (!quantities.contains(item)) {
    quantities.emplace(item, CRDTCounter<string, int>(node_id));
  }
  if (!checks.contains(item)) {
    checks.emplace(item, CRDTFlag<string>(node_id));
  }
}

void CRDTShoppingList::add_item(const string &item, const string &tag) {
  items.add(item, tag);
  ensure_item_structs_(item);
}

void CRDTShoppingList::remove_item(const string &item) {
  // Tombstone everything
  items.remove(item);
  
  auto qty_it = quantities.find(item);
  if (qty_it != quantities.end()) {
    auto& counter = qty_it->second;
    const auto& increments = counter.get_increments();
    auto decrements = counter.get_decrements();
    
    for (const auto& [node, inc_value] : increments) {
      auto dec_it = decrements.find(node);
      int dec_value = (dec_it != decrements.end()) ? dec_it->second : 0;
      if (inc_value > dec_value) {
        decrements[node] = inc_value;
      }
    }
    counter.set_decrements(decrements);
  }
  
  auto check_it = checks.find(item);
  if (check_it != checks.end()) {
    check_it->second.disable();
  }
}

void CRDTShoppingList::increment(const string &item, int amount) {
  if (!contains(item))
    return;
  ensure_item_structs_(item);
  auto iterator = quantities.find(item);
  if (iterator != quantities.end()) {
    iterator->second.increment(amount);
  }
}

void CRDTShoppingList::decrement(const string &item, int amount) {
  if (!contains(item))
    return;
  ensure_item_structs_(item);
  auto iterator = quantities.find(item);
  if (iterator != quantities.end()) {
    iterator->second.decrement(amount);
  }
}

int CRDTShoppingList::get_quantity(const string &item) const {
  if (!contains(item))
    return 0;
  const auto iterator = quantities.find(item);
  if (iterator == quantities.end())
    return 0;
  CRDTCounter<string, int> tmp = iterator->second;
  return tmp.get_value();
}

bool CRDTShoppingList::contains(const string &item) const {
  return items.contains(item);
}

unordered_set<string> CRDTShoppingList::get_item_names() const {
  return items.get_values();
}

void CRDTShoppingList::check(const string &item, const string &tag) {
  if (!contains(item))
    return;
  ensure_item_structs_(item);
  auto iterator = checks.find(item);
  if (iterator != checks.end()) {
    iterator->second.enable(tag);
  }
}

void CRDTShoppingList::uncheck(const string &item) {
  if (!contains(item))
    return;
  ensure_item_structs_(item);
  auto iterator = checks.find(item);
  if (iterator != checks.end()) {
    iterator->second.disable();
  }
}

bool CRDTShoppingList::is_checked(const string &item) const {
  if (!contains(item))
    return false;
  auto iterator = checks.find(item);
  if (iterator == checks.end())
    return false;
  return iterator->second.is_enabled();
}

bool CRDTShoppingList::hasActiveItems() const {
  // Check if any item has at least one add tag not in removes
  return !items.get_values().empty();
}

void CRDTShoppingList::merge(const CRDTShoppingList &other) {
  if (list_name != other.list_name)
    return;

  // Merge items
  items.merge(other.items);

  // Collect union of keys across items and per-item maps
  unordered_set<string> keys = items.get_values();
  for (const auto &pair : quantities)
    keys.insert(pair.first);
  for (const auto &pair : other.quantities)
    keys.insert(pair.first);
  for (const auto &pair : checks)
    keys.insert(pair.first);
  for (const auto &pair : other.checks)
    keys.insert(pair.first);

  for (const auto &k : keys) {
    // Merge counters if present in either
    auto itA = quantities.find(k);
    auto itB = other.quantities.find(k);
    if (itA == quantities.end() && itB != other.quantities.end()) {
      quantities.emplace(k, CRDTCounter<string, int>(node_id));
      itA = quantities.find(k);
    }
    if (itA != quantities.end() && itB != other.quantities.end()) {
      itA->second.merge(itB->second);
    }

    // Merge flags if present in either
    auto ftA = checks.find(k);
    auto ftB = other.checks.find(k);
    if (ftA == checks.end() && ftB != other.checks.end()) {
      checks.emplace(k, CRDTFlag<string>(node_id));
      ftA = checks.find(k);
    }
    if (ftA != checks.end() && ftB != other.checks.end()) {
      ftA->second.merge(ftB->second);
    }
  }
}

void to_json(nlohmann::json &j, const CRDTShoppingList &list) {
  // Convert quantities map
  nlohmann::json quantities_json = nlohmann::json::object();
  for (const auto &[item, counter] : list.get_quantities()) {
    quantities_json[item] = counter;
  }

  // Convert checks map
  nlohmann::json checks_json = nlohmann::json::object();
  for (const auto &[item, flag] : list.get_checks()) {
    checks_json[item] = flag;
  }

  j = nlohmann::json{{"node_id", list.id()},
                     {"list_name", list.name()},
                     {"items", list.get_items()},
                     {"quantities", quantities_json},
                     {"checks", checks_json}};
}

void from_json(const nlohmann::json &j, CRDTShoppingList &list) {
  list.set_node_id(j.value("node_id", string()));
  list.set_list_name(j.at("list_name").get<string>());
  list.set_items(j.at("items").get<CRDTSet<string, string>>());

  unordered_map<string, CRDTCounter<string, int>> quantities;
  for (auto &[key, val] : j.at("quantities").items()) {
    quantities[key] = val.get<CRDTCounter<string, int>>();
  }
  list.set_quantities(quantities);

  unordered_map<string, CRDTFlag<string>> checks;
  for (auto &[key, val] : j.at("checks").items()) {
    checks[key] = val.get<CRDTFlag<string>>();
  }
  list.set_checks(checks);
}

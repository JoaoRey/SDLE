#ifndef VNODE_H
#define VNODE_H

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

using namespace std;

struct VNode {
  // uuid:0, uuid:1
  string id;
  string physical_endpoint;
  // Position on the ring
  uint64_t position;

  VNode() = default;

  VNode(const string &id, const string &endpoint, uint64_t pos)
      : id(id), physical_endpoint(endpoint), position(pos) {}

  bool operator==(const VNode &other) const { return id == other.id; }

  bool operator<(const VNode &other) const { return position < other.position; }
};

void to_json(nlohmann::json &j, const VNode &v);

void from_json(const nlohmann::json &j, VNode &v);

#endif // VNODE_H
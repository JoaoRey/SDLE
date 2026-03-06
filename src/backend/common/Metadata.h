#ifndef METADATA_H
#define METADATA_H

#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "HashRing.h"
#include "VNode.h"

using namespace std;

class Metadata {
private:
  // Physical node to virtual node
  // endpoint, list vnodes
  map<string, vector<VNode>> nodes_;

  HashRing ring_;
  mutable mutex mutex_;

public:
  Metadata() = default;

  /// Add a physical node + vnodes to the ring
  void addNode(const string &endpoint, const vector<VNode> &vnodes);

  /// Remove a node + vnodes from the ring
  void removeNode(const string &endpoint);

  bool hasNode(const string &endpoint) const;

  vector<VNode> getVNodes(const string &endpoint) const;

  /// Get node endpoints responsible for a key
  vector<string> getNodesForKey(const string &key, size_t n) const;

  /// Get vnode IDs responsible for a key
  vector<string> getVNodesForKey(const string &key, size_t n) const;

  /// Get neighbor nodes around a vnode position
  vector<string> getNeighborNodesForVNode(uint64_t vnode_position,
                                         size_t n) const;

  vector<string> getAllEndpoints() const;

  size_t nodeCount() const;

  void clear();

  nlohmann::json toJson() const;

  void fromJson(const nlohmann::json &j);

private:
  /// Remove a node - caller function must hold lock
  void removeNodeUnsafe(const string &endpoint);
};

#endif // METADATA_H
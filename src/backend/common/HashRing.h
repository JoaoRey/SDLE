#ifndef HASH_RING_H
#define HASH_RING_H

#include <map>
#include <string>
#include <vector>
#include <xxhash.h>

using namespace std;

/// Each physical node has multiple virtual nodes.
/// For each one it holds part of the complete hash ring.
class HashRing {
private:
  // Map ring position/hash -> vnode_id
  map<uint64_t, string> ring_;

  // Map vnode_id -> node endpoint
  map<string, string> vnode_to_endpoint_;

public:
  HashRing() = default;

  /// Hash a key to a ring position
  static uint64_t hash(const string &key);

  void addVNode(const string &vnode_id, const string &physical_endpoint);

  void removeVNode(const string &vnode_id);

  void removeByEndpoint(const string &endpoint);

  /// Get the physical endpoint for a vnode
  string getEndpoint(const string &vnode_id) const;

  /// Get N node endpoints to know those responsible for a key
  /// Clockwise from the key hash position
  vector<string> getNodes(const string &key, size_t n) const;

  /// Get the preference list of vnodes
  vector<string> getVNodes(const string &key, size_t n) const;

  /// Get N neighbors (predecessors and successors)
  vector<string> getNeighborNodes(uint64_t vnode_position, size_t n) const;

  void clear();
};

#endif // HASH_RING_H

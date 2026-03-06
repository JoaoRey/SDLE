#include "HashRing.h"

#include <set>

using namespace std;

uint64_t HashRing::hash(const string &key) {
  return XXH64(key.c_str(), key.size(), 0);
}

void HashRing::addVNode(const string &vnode_id,
                        const string &physical_endpoint) {
  uint64_t position = hash(vnode_id);
  ring_[position] = vnode_id;
  vnode_to_endpoint_[vnode_id] = physical_endpoint;
}

void HashRing::removeVNode(const string &vnode_id) {
  uint64_t position = hash(vnode_id);
  ring_.erase(position);
  vnode_to_endpoint_.erase(vnode_id);
}

void HashRing::removeByEndpoint(const string &endpoint) {
  vector<string> to_remove;
  for (const auto &[vnode_id, ep] : vnode_to_endpoint_) {
    if (ep == endpoint) {
      to_remove.push_back(vnode_id);
    }
  }
  for (const auto &vnode_id : to_remove) {
    removeVNode(vnode_id);
  }
}

string HashRing::getEndpoint(const string &vnode_id) const {
  auto it = vnode_to_endpoint_.find(vnode_id);
  if (it != vnode_to_endpoint_.end()) {
    return it->second;
  }
  return "";
}

vector<string> HashRing::getNodes(const string &key, size_t n) const {
  std::set<string> result;

  if (ring_.empty()) {
    return {};
  }

  uint64_t key_hash = hash(key);

  // vnode at or after key position
  auto it = ring_.lower_bound(key_hash);

  // Walk the ring clockwise
  for (auto checked = 0; result.size() < n && checked < ring_.size();
       ++it, ++checked) {
    // Wrap around
    if (it == ring_.end()) {
      it = ring_.begin();
    }

    const string &vnode_id = it->second;
    string endpoint = getEndpoint(vnode_id);

    if (!endpoint.empty()) {
      result.insert(endpoint);
    }
  }

  return {result.begin(), result.end()};
}

vector<string> HashRing::getVNodes(const string &key, size_t n) const {
  std::set<string> result;

  if (ring_.empty()) {
    return {};
  }

  uint64_t key_hash = hash(key);

  // vnode at or after key position
  auto it = ring_.lower_bound(key_hash);

  for (auto checked = 0; result.size() < n && checked < ring_.size();
       ++it, ++checked) {
    if (it == ring_.end()) {
      it = ring_.begin();
    }

    const string &vnode_id = it->second;
    string endpoint = getEndpoint(vnode_id);

    if (!endpoint.empty()) {
      result.insert(vnode_id);
    }
  }

  return {result.begin(), result.end()};
}

vector<string> HashRing::getNeighborNodes(uint64_t vnode_position,
                                         size_t n) const {
  std::set<string> result;

  if (ring_.empty()) {
    return {};
  }

  // Find the vnode at this position
  auto center_it = ring_.find(vnode_position);
  if (center_it == ring_.end()) {
    return {};
  }

  // Get successors
  auto it = center_it;
  for (size_t i = 0; result.size() < n && i < ring_.size(); ++i) {
    ++it;
    if (it == ring_.end()) {
      it = ring_.begin();
    }
    if (it == center_it)
      break; // Full circle

    const string &vnode_id = it->second;
    string endpoint = getEndpoint(vnode_id);
    if (!endpoint.empty()) {
      result.insert(endpoint);
    }
  }

  // Get predecessors
  it = center_it;
  size_t pred_count = 0;
  for (size_t i = 0; pred_count < n && i < ring_.size(); ++i) {
    if (it == ring_.begin()) {
      it = ring_.end();
    }
    --it;
    if (it == center_it)
      break; // Full circle

    const string &vnode_id = it->second;
    string endpoint = getEndpoint(vnode_id);
    if (!endpoint.empty() && result.insert(endpoint).second) {
      pred_count++;
    }
  }

  return {result.begin(), result.end()};
}

void HashRing::clear() {
  ring_.clear();
  vnode_to_endpoint_.clear();
}

#include "Metadata.h"

using namespace std;

void Metadata::addNode(const string &endpoint, const vector<VNode> &vnodes) {
  lock_guard<mutex> lock(mutex_);
  // In case it is a re-registration
  removeNodeUnsafe(endpoint);

  nodes_[endpoint] = vnodes;
  for (const auto &vnode : vnodes) {
    ring_.addVNode(vnode.id, endpoint);
  }
}

void Metadata::removeNode(const string &endpoint) {
  lock_guard<mutex> lock(mutex_);
  removeNodeUnsafe(endpoint);
}

bool Metadata::hasNode(const string &endpoint) const {
  lock_guard<mutex> lock(mutex_);
  return nodes_.find(endpoint) != nodes_.end();
}

vector<VNode> Metadata::getVNodes(const string &endpoint) const {
  lock_guard<mutex> lock(mutex_);
  auto it = nodes_.find(endpoint);
  if (it != nodes_.end()) {
    return it->second;
  }
  return {};
}

vector<string> Metadata::getNodesForKey(const string &key, size_t n) const {
  lock_guard<mutex> lock(mutex_);
  return ring_.getNodes(key, n);
}

vector<string> Metadata::getVNodesForKey(const string &key, size_t n) const {
  lock_guard<mutex> lock(mutex_);
  return ring_.getVNodes(key, n);
}
vector<string> Metadata::getNeighborNodesForVNode(uint64_t vnode_position,
                                                  size_t n) const {
  lock_guard<mutex> lock(mutex_);
  return ring_.getNeighborNodes(vnode_position, n);
}
vector<string> Metadata::getAllEndpoints() const {
  lock_guard<mutex> lock(mutex_);
  vector<string> result;
  for (const auto &[endpoint, _] : nodes_) {
    result.push_back(endpoint);
  }
  return result;
}

size_t Metadata::nodeCount() const {
  lock_guard<mutex> lock(mutex_);
  return nodes_.size();
}

void Metadata::clear() {
  lock_guard<mutex> lock(mutex_);
  nodes_.clear();
  ring_.clear();
}

nlohmann::json Metadata::toJson() const {
  lock_guard<mutex> lock(mutex_);
  nlohmann::json j;
  j["nodes"] = nlohmann::json::object();

  for (const auto &[endpoint, vnodes] : nodes_) {
    j["nodes"][endpoint] = vnodes;
  }

  return j;
}

void Metadata::fromJson(const nlohmann::json &j) {
  lock_guard<mutex> lock(mutex_);

  nodes_.clear();
  ring_.clear();

  if (!j.contains("nodes")) {
    return;
  }

  for (auto &[endpoint, vnodes_json] : j["nodes"].items()) {
    vector<VNode> vnodes = vnodes_json.get<vector<VNode>>();
    nodes_[endpoint] = vnodes;
    for (const auto &vnode : vnodes) {
      ring_.addVNode(vnode.id, endpoint);
    }
  }
}

void Metadata::removeNodeUnsafe(const string &endpoint) {
  auto it = nodes_.find(endpoint);
  if (it != nodes_.end()) {
    for (const auto &vnode : it->second) {
      ring_.removeVNode(vnode.id);
    }
    nodes_.erase(it);
  }
}

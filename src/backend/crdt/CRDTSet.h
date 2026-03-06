#ifndef CRDT_CRDTSET_H
#define CRDT_CRDTSET_H

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <ostream>
#include <nlohmann/json.hpp>

using namespace std;

// TODO: If it makes sense optimize the tombstones
template<typename UUID=string, typename Value=string>
class CRDTSet {
private:
    UUID node_id;
    unordered_map<Value, unordered_set<UUID>> adds;
    unordered_map<Value, unordered_set<UUID>> removes;
public:
    CRDTSet() : node_id() {}
    explicit CRDTSet(UUID node_id): node_id(node_id) {};

    const UUID& get_node_id() const { return node_id; }
    const unordered_map<Value, unordered_set<UUID>>& get_adds() const { return adds; }
    const unordered_map<Value, unordered_set<UUID>>& get_removes() const { return removes; }

    void set_node_id(const UUID& id) { node_id = id; }
    void set_adds(const unordered_map<Value, unordered_set<UUID>>& a) { adds = a; }
    void set_removes(const unordered_map<Value, unordered_set<UUID>>& r) { removes = r; }

    void add(Value value, UUID tag);
    void remove(Value value);
    bool contains(Value value) const;
    unordered_set<Value> get_values() const;
    void merge(const CRDTSet& other);

    friend ostream& operator<<(ostream& out, const CRDTSet& crdt_set) {
        out << "Set: " << crdt_set.node_id << endl;

        out << "Adds: {" << endl;
        for (const auto& [value, tags] : crdt_set.adds) {
            out << "  " << value << ": {";
            bool first = true;
            for (const auto& tag : tags) {
                if (!first) out << ", ";
                out << tag;
                first = false;
            }
            out << "}" << endl;
        }
        out << "}" << endl;

        out << "Removes: {" << endl;
        for (const auto& [value, tags] : crdt_set.removes) {
            out << "  " << value << ": {";
            bool first = true;
            for (const auto& tag : tags) {
                if (!first) out << ", ";
                out << tag;
                first = false;
            }
            out << "}" << endl;
        }
        out << "}";

        return out;
    }

};

template<typename UUID, typename Value>
void CRDTSet<UUID, Value>::add(Value value, UUID tag) {
    adds[value].insert(tag);
}

template<typename UUID, typename Value>
void CRDTSet<UUID, Value>::remove(Value value) {
    auto iterator = adds.find(value);
    // Not found
    if (iterator == adds.end()) return;
    unordered_set<UUID> &tags = iterator->second;
    auto &removed_tags = removes[value];
    removed_tags.insert(tags.begin(), tags.end());
}

template<typename UUID, typename Value>
bool CRDTSet<UUID, Value>::contains(Value value) const {
    auto adds_iterator = adds.find(value);
    // Not found
    if (adds_iterator == adds.end()) return false;
    auto rem_iterator = removes.find(value);
    if (rem_iterator == removes.end()) return !adds_iterator->second.empty();

    for (const auto &tag : adds_iterator->second) {
        if (rem_iterator->second.find(tag) == rem_iterator->second.end())
            return true;
    }
    return false;
}

template<typename UUID, typename Value>
unordered_set<Value> CRDTSet<UUID, Value>::get_values() const {
    unordered_set<Value> out;
    for (const auto &[value, add_tags] : adds) {
        auto rem_iterator = removes.find(value);
        if (rem_iterator == removes.end()) {
            if (!add_tags.empty()) { // Not removed and has one add
                out.insert(value);
            }
        } else {
            const auto &rem_tags = rem_iterator->second;
            for (const auto &tag : add_tags) {
                if (rem_tags.find(tag) == rem_tags.end()) {
                    out.insert(value);
                    break;
                }
            }
        }
    }
    return out;
}

template<typename UUID, typename Value>
void CRDTSet<UUID, Value>::merge(const CRDTSet &other) {
    for (const auto &pair : other.adds) {
        auto &tags = adds[pair.first];
        tags.insert(pair.second.begin(), pair.second.end());
    }
    for (const auto &pair : other.removes) {
        auto &tags = removes[pair.first];
        tags.insert(pair.second.begin(), pair.second.end());
    }
}

template<typename UUID, typename Value>
void to_json(nlohmann::json& j, const CRDTSet<UUID, Value>& set) {
    // Convert unordered_map<Value, unordered_set<UUID>> to JSON object
    nlohmann::json adds_json = nlohmann::json::object();
    for (const auto& [value, tags] : set.get_adds()) {
        adds_json[value] = tags;
    }
    nlohmann::json removes_json = nlohmann::json::object();
    for (const auto& [value, tags] : set.get_removes()) {
        removes_json[value] = tags;
    }
    j = nlohmann::json{
        {"node_id", set.get_node_id()},
        {"adds", adds_json},
        {"removes", removes_json}
    };
}

template<typename UUID, typename Value>
void from_json(const nlohmann::json& j, CRDTSet<UUID, Value>& set) {
    set.set_node_id(j.value("node_id", UUID()));
    
    unordered_map<Value, unordered_set<UUID>> adds;
    for (auto& [key, val] : j.at("adds").items()) {
        adds[key] = val.get<unordered_set<UUID>>();
    }
    set.set_adds(adds);
    
    unordered_map<Value, unordered_set<UUID>> removes;
    for (auto& [key, val] : j.at("removes").items()) {
        removes[key] = val.get<unordered_set<UUID>>();
    }
    set.set_removes(removes);
}


#endif


#ifndef CRDT_CRDTCOUNTER_H
#define CRDT_CRDTCOUNTER_H

#include <map>
#include <string>
#include <ostream>
#include <nlohmann/json.hpp>

using namespace std;

template <typename UUID=string, typename Value=int>
class CRDTCounter {
private:
    UUID node_id;
    map<UUID, Value> increments;
    map<UUID, Value> decrements;

public:
    CRDTCounter() : node_id() {}
    explicit CRDTCounter(UUID id) : node_id(id) {}

    const UUID& get_node_id() const { return node_id; }
    const map<UUID, Value>& get_increments() const { return increments; }
    const map<UUID, Value>& get_decrements() const { return decrements; }

    void set_node_id(const UUID& id) { node_id = id; }
    void set_increments(const map<UUID, Value>& inc) { increments = inc; }
    void set_decrements(const map<UUID, Value>& dec) { decrements = dec; }

    void increment(Value amount = 1);

    void decrement(Value amount = 1);

    Value get_value();

    void merge(const CRDTCounter& other);

    friend ostream& operator<<(ostream& out, const CRDTCounter& counter) {
        out << "Counter: " << counter.node_id << endl;

        out << "Increments: {";
        bool first = true;
        for (const auto& [id, val] : counter.increments) {
            if (!first) out << ", ";
            out << id << ": " << val;
            first = false;
        }
        out << "}" << endl;

        out << " Decrements: {";
        first = true;
        for (const auto& [id, val] : counter.decrements) {
            if (!first) out << ", ";
            out << id << ": " << val;
            first = false;
        }
        out << "}";

        return out;
    }

};

template<typename UUID, typename Value>
void CRDTCounter<UUID,Value>::increment(Value amount) {
    increments[node_id] += amount;
}

template<typename UUID, typename Value>
void CRDTCounter<UUID, Value>::decrement(Value amount) {
    decrements[node_id] += amount;
}

template<typename UUID, typename Value>
Value CRDTCounter<UUID, Value>::get_value() {
    Value total_inc = 0;
    for (const auto& pair : increments)
        total_inc += pair.second;
    Value total_dec = 0;
    for (const auto& pair : decrements)
        total_dec += pair.second;
    return total_inc - total_dec;
}

template<typename UUID, typename Value>
void CRDTCounter<UUID, Value>::merge(const CRDTCounter &other) {
    for (const auto& [id, val] : other.increments) {
        auto& cur = increments[id];
        if (cur < val) cur = val;
    }
    for (const auto& [id, val] : other.decrements) {
        auto& cur = decrements[id];
        if (cur < val) cur = val;
    }
}

template<typename UUID, typename Value>
void to_json(nlohmann::json& j, const CRDTCounter<UUID, Value>& counter) {
    j = nlohmann::json{
        {"node_id", counter.get_node_id()},
        {"increments", counter.get_increments()},
        {"decrements", counter.get_decrements()}
    };
}

template<typename UUID, typename Value>
void from_json(const nlohmann::json& j, CRDTCounter<UUID, Value>& counter) {
    counter.set_node_id(j.value("node_id", UUID()));
    counter.set_increments(j.at("increments").get<map<UUID, Value>>());
    counter.set_decrements(j.at("decrements").get<map<UUID, Value>>());
}

#endif //CRDT_CRDTCOUNTER_H

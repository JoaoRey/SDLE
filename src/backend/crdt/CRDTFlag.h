#ifndef CRDT_CRDTFLAG_H
#define CRDT_CRDTFLAG_H

#include <string>
#include <unordered_set>
#include <ostream>
#include <nlohmann/json.hpp>

using namespace std;

/// Enable-Wins Flag (EWFlag)
/// The flag is enabled if there are enables that happened after the last disable.
/// The tags must be unique to ensure correct behavior (uuid)
template <typename UUID=string>
class CRDTFlag {
private:
    UUID node_id;
    unordered_set<UUID> enables;
    unordered_set<UUID> disables;
public:
    CRDTFlag() : node_id() {}
    explicit CRDTFlag(UUID id): node_id(id) {
        enables.insert(id);
    }

    const UUID& get_node_id() const { return node_id; }
    const unordered_set<UUID>& get_enables() const { return enables; }
    const unordered_set<UUID>& get_disables() const { return disables; }

    void set_node_id(const UUID& id) { node_id = id; }
    void set_enables(const unordered_set<UUID>& e) { enables = e; }
    void set_disables(const unordered_set<UUID>& d) { disables = d; }

    void enable(UUID tag) {
        enables.insert(tag);
    }

    void disable() {
        // Copy all current enables to disables to propagate the disable
        disables.insert(enables.begin(), enables.end());
        enables.clear();
    }

    [[nodiscard]] bool is_enabled() const {
        return !enables.empty();
    }

    void merge(const CRDTFlag &other) {
        // Merge enables and disables from other
        enables.insert(other.enables.begin(), other.enables.end());
        disables.insert(other.disables.begin(), other.disables.end());

        // Remove any enables that are in the merged disables set
        for (const auto &disabled_tag : disables) {
            enables.erase(disabled_tag);
        }
    }

    friend ostream &operator<<(ostream &out, const CRDTFlag &flag) {
        out << "Flag: " << flag.node_id << '\n';
        out << "Enables: {";
        bool first = true;
        for (const auto &tag : flag.enables) {
            if (!first) out << ", ";
            out << tag;
            first = false;
        }
        out << "}\nDisables: {";
        first = true;
        for (const auto &tag : flag.disables) {
            if (!first) out << ", ";
            out << tag;
            first = false;
        }
        out << "}";
        return out;
    }
};

template<typename UUID>
void to_json(nlohmann::json& j, const CRDTFlag<UUID>& flag) {
    j = nlohmann::json{
        {"node_id", flag.get_node_id()},
        {"enables", flag.get_enables()},
        {"disables", flag.get_disables()}
    };
}

template<typename UUID>
void from_json(const nlohmann::json& j, CRDTFlag<UUID>& flag) {
    flag.set_node_id(j.value("node_id", UUID()));
    flag.set_enables(j.at("enables").get<unordered_set<UUID>>());
    flag.set_disables(j.at("disables").get<unordered_set<UUID>>());
}

#endif //CRDT_CRDTFLAG_H

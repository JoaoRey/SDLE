#include "VNode.h"

using namespace std;

void from_json(const nlohmann::json &j, VNode &v) {
  j.at("id").get_to(v.id);
  j.at("physical_endpoint").get_to(v.physical_endpoint);
  j.at("position").get_to(v.position);
}

void to_json(nlohmann::json &j, const VNode &v) {
  j = nlohmann::json{{"id", v.id},
                     {"physical_endpoint", v.physical_endpoint},
                     {"position", v.position}};
}
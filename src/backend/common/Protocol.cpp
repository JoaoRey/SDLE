#include "Protocol.h"

using namespace std;

string messageTypeToString(MessageType type) {
  switch (type) {
  case MessageType::REGISTER:
    return "REGISTER";
  case MessageType::UNREGISTER:
    return "UNREGISTER";
  case MessageType::HEARTBEAT:
    return "HEARTBEAT";
  case MessageType::SYNC_METADATA:
    return "SYNC_METADATA";
  case MessageType::REGISTER_ACK:
    return "REGISTER_ACK";
  case MessageType::GET:
    return "GET";
  case MessageType::PUT:
    return "PUT";
  case MessageType::DELETE:
    return "DELETE";
  case MessageType::REPLICATE:
    return "REPLICATE";
  case MessageType::READ_REPAIR:
    return "READ_REPAIR";
  case MessageType::SYNC_REQUEST:
    return "SYNC_REQUEST";
  case MessageType::LIST_NODES:
    return "LIST_NODES";
  case MessageType::STOP_NODE:
    return "STOP_NODE";
  case MessageType::PING:
    return "PING";
  case MessageType::NODE_STATUS:
    return "NODE_STATUS";
  case MessageType::GET_STORAGE_KEYS:
    return "GET_STORAGE_KEYS";
  case MessageType::GET_STORAGE_VALUE:
    return "GET_STORAGE_VALUE";
  case MessageType::GET_RING_STATE:
    return "GET_RING_STATE";
  case MessageType::RESPONSE:
    return "RESPONSE";
  default:
    return "UNKNOWN";
  }
}

MessageType stringToMessageType(const string &str) {
  if (str == "REGISTER")
    return MessageType::REGISTER;
  if (str == "UNREGISTER")
    return MessageType::UNREGISTER;
  if (str == "HEARTBEAT")
    return MessageType::HEARTBEAT;
  if (str == "SYNC_METADATA")
    return MessageType::SYNC_METADATA;
  if (str == "REGISTER_ACK")
    return MessageType::REGISTER_ACK;
  if (str == "GET")
    return MessageType::GET;
  if (str == "PUT")
    return MessageType::PUT;
  if (str == "DELETE")
    return MessageType::DELETE;
  if (str == "REPLICATE")
    return MessageType::REPLICATE;
  if (str == "READ_REPAIR")
    return MessageType::READ_REPAIR;
  if (str == "SYNC_REQUEST")
    return MessageType::SYNC_REQUEST;
  if (str == "LIST_NODES")
    return MessageType::LIST_NODES;
  if (str == "STOP_NODE")
    return MessageType::STOP_NODE;
  if (str == "PING")
    return MessageType::PING;
  if (str == "NODE_STATUS")
    return MessageType::NODE_STATUS;
  if (str == "GET_STORAGE_KEYS")
    return MessageType::GET_STORAGE_KEYS;
  if (str == "GET_STORAGE_VALUE")
    return MessageType::GET_STORAGE_VALUE;
  if (str == "GET_RING_STATE")
    return MessageType::GET_RING_STATE;
  if (str == "RESPONSE")
    return MessageType::RESPONSE;
  return MessageType::REGISTER; // Default fallback
}

Message::Message(MessageType t, const string &sender, const string &req_id)
    : type(t), sender_endpoint(sender), request_id(req_id) {}

nlohmann::json Message::toJson() const {
  return {{"type", messageTypeToString(type)},
          {"sender_endpoint", sender_endpoint},
          {"request_id", request_id},
          {"payload", payload}};
}

Message Message::fromJson(const nlohmann::json &j) {
  Message msg;
  msg.type = stringToMessageType(j.at("type").get<string>());
  msg.sender_endpoint = j.value("sender_endpoint", "");
  msg.request_id = j.value("request_id", "");
  msg.payload = j.value("payload", nlohmann::json::object());
  return msg;
}

string Message::serialize() const { return toJson().dump(); }

Message Message::deserialize(const string &data) {
  return fromJson(nlohmann::json::parse(data));
}

Message createRegisterMessage(const string &endpoint,
                              const vector<VNode> &vnodes) {
  Message msg(MessageType::REGISTER, endpoint);
  msg.payload["vnodes"] = vnodes;
  return msg;
}

Message createUnregisterMessage(const string &endpoint) {
  Message msg(MessageType::UNREGISTER, endpoint);
  return msg;
}

Message createHeartbeatMessage(const string &endpoint) {
  Message msg(MessageType::HEARTBEAT, endpoint);
  return msg;
}

Message createSyncMetadataMessage(const string &sender,
                                  const nlohmann::json &metadata) {
  Message msg(MessageType::SYNC_METADATA, sender);
  msg.payload["metadata"] = metadata;
  return msg;
}

Message createRegisterAckMessage(const string &sender,
                                 const nlohmann::json &metadata) {
  Message msg(MessageType::REGISTER_ACK, sender);
  msg.payload["metadata"] = metadata;
  return msg;
}

Message createGetMessage(const string &sender, const string &list_name,
                         const string &req_id) {
  Message msg(MessageType::GET, sender, req_id);
  msg.payload["list_name"] = list_name;
  return msg;
}

Message createPutMessage(const string &sender, const string &list_name,
                         const nlohmann::json &crdt_data,
                         const string &req_id) {
  Message msg(MessageType::PUT, sender, req_id);
  msg.payload["list_name"] = list_name;
  msg.payload["data"] = crdt_data;
  return msg;
}

Message createDeleteMessage(const string &sender, const string &list_name,
                            const string &req_id) {
  Message msg(MessageType::DELETE, sender, req_id);
  msg.payload["list_name"] = list_name;
  return msg;
}

Message createReplicateMessage(const string &sender, const string &list_name,
                               const nlohmann::json &crdt_data,
                               const string &req_id) {
  Message msg(MessageType::REPLICATE, sender, req_id);
  msg.payload["list_name"] = list_name;
  msg.payload["data"] = crdt_data;
  return msg;
}

Message createReadRepairMessage(const string &sender, const string &list_name,
                                const nlohmann::json &crdt_data) {
  Message msg(MessageType::READ_REPAIR, sender);
  msg.payload["list_name"] = list_name;
  msg.payload["data"] = crdt_data;
  return msg;
}

Message createSyncRequestMessage(const string &sender, const string &req_id) {
  return Message(MessageType::SYNC_REQUEST, sender, req_id);
}

Message createListNodesMessage(const string &sender, const string &req_id) {
  Message msg(MessageType::LIST_NODES, sender, req_id);
  return msg;
}

Message createStopNodeMessage(const string &sender,
                              const string &target_endpoint,
                              const string &req_id) {
  Message msg(MessageType::STOP_NODE, sender, req_id);
  msg.payload["target_endpoint"] = target_endpoint;
  return msg;
}

Message createPingMessage(const string &sender, const string &req_id) {
  return Message(MessageType::PING, sender, req_id);
}

Message createNodeStatusMessage(const string &sender, const string &req_id) {
  return Message(MessageType::NODE_STATUS, sender, req_id);
}

Message createGetStorageKeysMessage(const string &sender, int limit,
                                    const string &req_id) {
  Message msg(MessageType::GET_STORAGE_KEYS, sender, req_id);
  msg.payload = {{"limit", limit}};
  return msg;
}

Message createGetStorageValueMessage(const string &sender, const string &key,
                                     const string &req_id) {
  Message msg(MessageType::GET_STORAGE_VALUE, sender, req_id);
  msg.payload = {{"key", key}};
  return msg;
}

Message createGetRingStateMessage(const string &sender, const string &req_id) {
  return Message(MessageType::GET_RING_STATE, sender, req_id);
}

Message createResponseMessage(const string &sender, StatusCode status,
                              const string &req_id, const nlohmann::json &data,
                              int retry_after_ms) {
  Message msg(MessageType::RESPONSE, sender, req_id);
  msg.payload["status"] = static_cast<int>(status);
  if (!data.is_null()) {
    msg.payload["data"] = data;
  }
  // Include retry-after so client knows when to retry
  if (status == StatusCode::UNAVAILABLE && retry_after_ms > 0) {
    msg.payload["retry_after_ms"] = retry_after_ms;
  }
  return msg;
}

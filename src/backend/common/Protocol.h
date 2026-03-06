#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "VNode.h"
#include <nlohmann/json.hpp>
#include <string>

using namespace std;

enum class MessageType {
  // Node -> Router
  // Node registers
  REGISTER,
  UNREGISTER,
  HEARTBEAT,

  // Router -> Node
  // Router sends ring metadata
  SYNC_METADATA,
  REGISTER_ACK,

  // Client(through router)/Node -> Node
  GET,
  PUT,
  DELETE,

  // Node -> Node
  REPLICATE,
  // When read quorum shows a replica is out of sync
  READ_REPAIR,
  // Request data sync from another node
  // When a node first joins - it does not know which keys it should have
  // Overlaps with push to node on SYNC_METADATA
  // It acts as a PULL to N predecessors + N successors
  SYNC_REQUEST,

  // Admin commands
  LIST_NODES,
  STOP_NODE,
  PING,

  // Debug / Admin
  NODE_STATUS,
  GET_STORAGE_KEYS,
  GET_STORAGE_VALUE,
  GET_RING_STATE,

  RESPONSE
};

string messageTypeToString(MessageType type);

MessageType stringToMessageType(const string &str);

/// Status codes for responses
enum class StatusCode {
  OK = 200,
  NOT_FOUND = 404,
  INTERNAL_ERROR = 500,
  UNAVAILABLE = 503,
};

struct Message {
  MessageType type;
  string sender_endpoint;
  string request_id;
  nlohmann::json payload;

  Message() = default;

  Message(MessageType t, const string &sender, const string &req_id = "");

  nlohmann::json toJson() const;

  static Message fromJson(const nlohmann::json &j);

  string serialize() const;

  static Message deserialize(const string &data);
};

Message createRegisterMessage(const string &endpoint,
                              const vector<VNode> &vnodes);

Message createUnregisterMessage(const string &endpoint);

Message createHeartbeatMessage(const string &endpoint);

Message createSyncMetadataMessage(const string &sender,
                                  const nlohmann::json &metadata);

Message createRegisterAckMessage(const string &sender,
                                 const nlohmann::json &metadata);

Message createGetMessage(const string &sender, const string &list_name,
                         const string &req_id = "");

Message createPutMessage(const string &sender, const string &list_name,
                         const nlohmann::json &crdt_data,
                         const string &req_id = "");

Message createDeleteMessage(const string &sender, const string &list_name,
                            const string &req_id = "");

Message createReplicateMessage(const string &sender, const string &list_name,
                               const nlohmann::json &crdt_data,
                               const string &req_id = "");

Message createReadRepairMessage(const string &sender, const string &list_name,
                                const nlohmann::json &crdt_data);

Message createSyncRequestMessage(const string &sender, const string &req_id = "");

Message createListNodesMessage(const string &sender, const string &req_id = "");

Message createStopNodeMessage(const string &sender,
                              const string &target_endpoint,
                              const string &req_id = "");

Message createPingMessage(const string &sender, const string &req_id = "");

Message createNodeStatusMessage(const string &sender,
                                const string &req_id = "");

Message createGetStorageKeysMessage(const string &sender, int limit,
                                    const string &req_id = "");

Message createGetStorageValueMessage(const string &sender, const string &key,
                                     const string &req_id = "");

Message createGetRingStateMessage(const string &sender,
                                  const string &req_id = "");

Message createResponseMessage(const string &sender, StatusCode status,
                              const string &req_id = "",
                              const nlohmann::json &data = nullptr,
                              int retry_after_ms = -1);

#endif // PROTOCOL_H
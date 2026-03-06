# Project Overview: Shopping Lists on the Cloud

## Introduction

This project implements a **local-first distributed shopping list application**. The system combines offline-first clients with a backend inspired by Amazon Dynamo's architecture. It is designed to ensure high availability, scalability, and conflict-free data synchronization using Conflict-free Replicated Data Types (CRDTs), without compromising data consistency.

## System Architecture

#### 1. Local-First Client (NextJS)
- **Shopping List Application**: Runs on the user's device, allowing entirely offline operation. Changes are kept in browser local storage and synchronized with the backend when connectivity is available.

#### 2. Distributed Backend (C++)
- **Router Server**: Entry point that routes requests using consistent hashing (Router/Dealer pattern)
- **Node Servers**: Distributed storage nodes responsible for data persistence, replication and self-coordination (Req/Rep pattern)
- **Admin Console**: Tool for managing and monitoring the backend.

Lists are distributed across nodes based on `list_id` using a hash ring with virtual nodes. Data persistence is handled via SQLite databases on each node. Communication between components is implemented using ZeroMQ.


## Conflict Resolution
The system implements our own **custom Conflict-free Replicated Data Types**:

- **CRDTCounter**: Handles increment/decrement operations for item quantities
- **CRDTSet**: Manages item addition and removal without conflicts through the usage of tombstones
- **CRDTFlag**: Manages boolean states (EnableWins implementation). Flag is enabled for any enabled operation since last disable it received.
- **CRDTShoppingList**: complete list state type that aggregates the above CRDTs.

## Technical Implementation

### Backend (C++)
- **Communication**: ZeroMQ (Dealer/Router and Req/Rep patterns)
- **Storage**: SQLite for persistent storage on each node
- **JSON Serialization**: nlohmann_json for message formatting
- **Hashing**: xxHash for fast hashing
- The communication is asynchronous and non-blocking.

### Replication Strategy
- **Replication Factor (N)**: Each write is replicated to 3 nodes
- **Quorum-based reads/writes**: Configurable R/W parameters (R=2, W=2)
- **Read repair**: Automatic correction of stale/missing replicas during reads
- **Topology management**: Router acts as a central authority for the ring's topology. Nodes register/deregister with the Router and send heartbeats to indicate liveness.
- **Synchronization on topology changes**: 
1. Existing nodes push lists they contain and are no longer responsible for to nodes in the ring (this happens for new nodes and existing nodes). 
2. New nodes request from N successors and N predecessors for the data they should own. Also happens when there is a large topology change ( 50% for testing purposes). 

These 2 scenarios can overlap in time, but CRDTs are idempotent and that leads to faster consistency. Synchronization between 2 nodes happens a single request/response to ensure low communication overhead. 

## Scalability

- **Consistent hashing**: Distributes load evenly across nodes
- **Virtual nodes**: Each physical node manages multiple positions on the hash ring
- **Partitioning**: Lists are independent units
- **Decentralized**: No master node or coordination. Nodes are self coordinating. While the Router is a single point of failure, nodes and web client can continue operating if the router is down and `admin_console` handles the restarts on failure. 
ains operational during node failures

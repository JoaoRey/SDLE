# Build and Run Instructions

This document provides step-by-step instructions to build and run the project.

## Prerequisites

### Dependencies

The following libraries must be installed on your system:
- **C++ Compiler**
- **CMake**
- **ZeroMQ** (`cppzmq`)
- **nlohmann_json**
- **libxxhash**
- **SQLite3**
- **libuuid**
- **npm** 

#### Installation Commands

**macOS (using Homebrew):**
```bash
brew install cppzmq nlohmann-json xxhash sqlite ossp-uuid cmake
```

**Ubuntu (using apt):**
```bash
sudo apt update
sudo apt install build-essential libcppzmq-dev libnlohmann-json-dev \
                 libxxhash-dev libsqlite3-dev uuid-dev cmake
```

**Fedora (using dnf):**
```bash
sudo dnf install gcc-c++ cmake cppzmq-devel json-devel xxhash-devel \
                 sqlite-devel libuuid-devel
```

## Building the Project

### Clone the Repository
```bash
git clone git@gitlab.up.pt:classes/sdle/2025/t3/g04.git
cd g04
```

You are now in the project's root directory.

### Backend

#### 1. Configure with CMake
```bash
cmake -B src/backend/build -S src/backend
```

This command creates a `build` directory and configures the project.

#### 2. Compile
```bash
cmake --build src/backend/build
```

This compiles all backend components:
- `router_server`
- `node_server`
- `admin_console`
- `client_cli`
- Test executables (`crdt_tests`, `system_test_client`)

The build process automatically creates a `release` directory with all executables and configuration files:

```bash
ls src/backend/build/release
```

Expected contents:
- `admin_console`
- `router_server`
- `node_server`
- `client_cli`
- `config/` directory with JSON configuration files

### Frontend

#### 1. Install Dependencies
```bash
npm install --prefix src/shopping-list-app
```

#### 2. Build the Application
```bash
npm run build --prefix src/shopping-list-app
```

## Running the System

### Backend

#### Recommended Workflow

The best way to run the backend is using the **Admin Console** as a process manager.

##### Step 1: Start the Admin Console
```bash
cd src/backend/build/release
./admin_console --main
```

The `--main` flag enables process management capabilities.

##### Step 2: Initialize the Cluster (if not using --main)

Within the Admin Console, execute the following commands:

```bash
# Start the router server
start router

# Start 3 node servers (minimum for N=3 replication)
start node 5556
start node 5557
start node 5558

# Verify the topology
status
list nodes
```

##### Step 3: Launch the Client (Optional)
In a **new terminal window** from the project root:

```bash
./src/backend/build/release/client_cli
```

#### Manual Execution (Alternative)

If you prefer not to use the Admin Console process manager:

##### Start Router Server
```bash
./src/backend/build/release/router_server --config src/backend/build/release/config/router_config.json
```

##### Start Node Servers
Open separate terminal windows for each node:

```bash
# Terminal 1
./src/backend/build/release/node_server --config src/backend/build/release/config/node_config.json --port 5556 --router tcp://127.0.0.1:5555

# Terminal 2
./src/backend/build/release/node_server --config src/backend/build/release/config/node_config.json --port 5557 --router tcp://127.0.0.1:5555

# Terminal 3
./src/backend/build/release/node_server --config src/backend/build/release/config/node_config.json --port 5558 --router tcp://127.0.0.1:5555
```

### Frontend

#### Start the Next.js Application
```bash
npm start --prefix src/shopping-list-app -- -p 3001
```

The frontend will be available at `http://localhost:3001`.


## Using the System

### Admin Console Commands

| Command | Description |
|---------|-------------|
| `start router` | Start the router server |
| `start node <port>` | Start a node server on the specified port |
| `list nodes` | Display all nodes in the hash ring |
| `list routers` | Show managed router processes |
| `list pids` | Show all managed processes |
| `stop <pid>` | Stop a specific process or node |
| `stop pids all` | Stop all managed processes |
| `attach <pid>` | View live logs (press `q` to exit) |
| `status` | Check router connection and list nodes |
| `node-status <endpoint>` | Get detailed status from a node |
| `node-keys <endpoint> [limit]` | List keys stored on a node |
| `node-inspect <endpoint> <key>` | View raw CRDT data for a key |
| `ring-check <endpoint>` | View hash ring state from a node |
| `exit` | Exit the admin console |

### Client CLI Commands

| Command | Description |
|---------|-------------|
| `create <name>` | Create and open a new shopping list |
| `open <name>` | Open an existing list |
| `show` | Display current list (auto-syncs) |
| `add <item> [qty]` | Add an item with optional quantity |
| `check <item>` | Mark item as checked |
| `uncheck <item>` | Mark item as unchecked |
| `del <item>` | Delete an item |
| `delete_list` | Permanently delete the current list |
| `sync` | Manually sync with the server |
| `clear` | Clear the terminal screen |
| `exit` | Exit the client |

### Example Usage Session

```bash
# In the client CLI:
create GroceryList
add Milk 2
add Eggs 12
add Bread 1
show
check Milk
show
```

## Testing

### CRDT Unit Tests
```bash
./src/backend/build/release/crdt_tests
```

### System Integration Tests
```bash
./src/backend/build/release/system_test_client
```

## Configuration

### Backend Configuration

Configuration files are located in `src/backend/build/release/config/`:

- **`router_config.json`**: Router server settings (port, heartbeat interval)
- **`node_config.json`**: Node server settings (replication factor, quorum parameters, storage path)

### Key Configuration Parameters

**Node Configuration (`node_config.json`):**
- `replication_factor`: Number of replicas per write (default: 3)
- `read_quorum`: Number of nodes required for read consensus
- `write_quorum`: Number of nodes required for write consensus
- `virtual_nodes`: Number of virtual nodes per physical node on the hash ring

### Logs and Debugging

**View process logs via Admin Console:**
```bash
attach <pid>
```
or manually check log files in `src/backend/build/release/logs/`.

## Stopping the System

### Using Admin Console
```bash
exit
```

### Manual Shutdown
Press `Ctrl+C` in each terminal window running a component.
cmake_minimum_required(VERSION 3.28)
project(backend)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CPPZMQ REQUIRED IMPORTED_TARGET cppzmq)

# JSON
find_package(nlohmann_json REQUIRED)
# xxHash
pkg_check_modules(XXHASH REQUIRED IMPORTED_TARGET libxxhash)
# SQLite3
find_package(SQLite3 REQUIRED)
# UUID
pkg_check_modules(UUID REQUIRED IMPORTED_TARGET uuid)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Common Library
set(COMMON_SOURCES
    common/HashRing.cpp
    common/Metadata.cpp
    common/Protocol.cpp
    common/VNode.cpp
    common/Logger.cpp
)
add_library(common STATIC ${COMMON_SOURCES})
target_link_libraries(common PRIVATE
    PkgConfig::XXHASH
    nlohmann_json::nlohmann_json
)

# CRDT Library
add_library(crdt STATIC
    crdt/CRDTShoppingList.cpp
)
target_link_libraries(crdt PRIVATE
    nlohmann_json::nlohmann_json
)

add_executable(router_server router_server/main.cpp)
target_link_libraries(router_server PRIVATE 
    common
    PkgConfig::CPPZMQ 
    PkgConfig::XXHASH 
    SQLite::SQLite3
    nlohmann_json::nlohmann_json
)

add_executable(node_server node_server/main.cpp node_server/NodeServer.cpp)
target_link_libraries(node_server PRIVATE 
    common
    crdt
    PkgConfig::CPPZMQ 
    PkgConfig::XXHASH 
    SQLite::SQLite3
    nlohmann_json::nlohmann_json
    PkgConfig::UUID
)

set_target_properties(router_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set_target_properties(node_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Bundle Logic
# Copy router_server to release
add_custom_command(TARGET router_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/release
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/release/config
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/release/logs
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:router_server> ${CMAKE_BINARY_DIR}/release/router_server
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/router_server/config.json ${CMAKE_BINARY_DIR}/release/config/router_config.json
    COMMENT "Copying router_server and config to release directory"
)

# Copy node_server to release
add_custom_command(TARGET node_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/release
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/release/config
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:node_server> ${CMAKE_BINARY_DIR}/release/node_server
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/node_server/config.json ${CMAKE_BINARY_DIR}/release/config/node_config.json
    COMMENT "Copying node_server and config to release directory"
)


##TESTS SETUP##

set(GOOGLETEST_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Google_tests")
set(GOOGLETEST_BIN_DIR "${CMAKE_BINARY_DIR}/Google_tests-build")

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory("${GOOGLETEST_SRC_DIR}" "${GOOGLETEST_BIN_DIR}")

# Enable CTest integration
enable_testing()

# Collect CRDT test sources and define the test executable
file(GLOB_RECURSE CRDT_TEST_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/tests/crdt/*.cpp)

add_executable(crdt_tests ${CRDT_TEST_SOURCES})
target_link_libraries(crdt_tests PRIVATE GTest::gtest_main nlohmann_json::nlohmann_json crdt)

target_include_directories(crdt_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/crdt"
)

# Keep test binary alongside other executables
set_target_properties(crdt_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Auto-discover and register GoogleTest tests
include(GoogleTest)
gtest_discover_tests(crdt_tests)

# System Test Client
add_executable(system_test_client tests/system_test_client.cpp)
target_link_libraries(system_test_client PRIVATE 
    common
    crdt
    PkgConfig::CPPZMQ 
    nlohmann_json::nlohmann_json
)
target_include_directories(system_test_client PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/crdt"
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
)
set_target_properties(system_test_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Admin Console
add_executable(admin_console
    admin_console/main.cpp
    admin_console/ProcessManager.cpp
    admin_console/RouterClient.cpp
    admin_console/TopologyManager.cpp
)
target_link_libraries(admin_console PRIVATE 
    common
    PkgConfig::CPPZMQ 
    nlohmann_json::nlohmann_json
)
target_include_directories(admin_console PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
)
set_target_properties(admin_console PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Copy admin_console to release
add_custom_command(TARGET admin_console POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/release
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/release/config
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:admin_console> ${CMAKE_BINARY_DIR}/release/admin_console
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/config/topology.json ${CMAKE_BINARY_DIR}/release/config/topology.json
    COMMENT "Copying admin_console and config to release directory"
)
# Client CLI
add_executable(client_cli
    client_cli/main.cpp
    client_cli/ClientCLI.cpp
    client_cli/ShoppingListClient.cpp
)
target_link_libraries(client_cli PRIVATE 
    common
    crdt
    PkgConfig::CPPZMQ 
    nlohmann_json::nlohmann_json
)
target_include_directories(client_cli PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/crdt"
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
)
set_target_properties(client_cli PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Copy to release
add_custom_command(TARGET client_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:client_cli> ${CMAKE_BINARY_DIR}/release/client_cli
    COMMENT "Copying client_cli to release directory"
)

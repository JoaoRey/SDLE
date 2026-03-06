#ifndef NODE_STORAGE_H
#define NODE_STORAGE_H

#include "../crdt/CRDTShoppingList.h"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <sqlite3.h>
#include <string>
#include <vector>

using namespace std;

class Storage {
private:
  sqlite3 *db_ = nullptr;
  string db_path_;

  bool execute(const string &sql) {
    char *err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
      string error = err_msg ? err_msg : "Unknown error";
      sqlite3_free(err_msg);
      return false;
    }
    return true;
  }

public:
  Storage() = default;
  ~Storage() { close(); }

  // Non-copyable
  Storage(const Storage &) = delete;
  Storage &operator=(const Storage &) = delete;

  // Movable
  Storage(Storage &&other) noexcept
      : db_(other.db_), db_path_(std::move(other.db_path_)) {
    other.db_ = nullptr;
  }

  Storage &operator=(Storage &&other) noexcept {
    if (this != &other) {
      close();
      db_ = other.db_;
      db_path_ = std::move(other.db_path_);
      other.db_ = nullptr;
    }
    return *this;
  }

  bool open(const string &data_dir, const string &node_id) {
    filesystem::create_directories(data_dir);

    db_path_ = data_dir + "/" + node_id + ".db";

    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
      return false;
    }

    // Create the shopping_lists table
    const string create_table = R"(
            CREATE TABLE IF NOT EXISTS shopping_lists (
                list_name TEXT PRIMARY KEY,
                crdt_json TEXT NOT NULL,
                updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                is_deleted INTEGER DEFAULT 0
            )
        )";

    return execute(create_table);
  }

  void close() {
    if (db_) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
  }

  bool is_open() const { return db_ != nullptr; }

  /// Save a shopping list (insert or update)
  bool save(const string &list_name, const CRDTShoppingList &list, bool is_deleted = false) {
    if (!db_)
      return false;

    const string sql = R"(
            INSERT INTO shopping_lists (list_name, crdt_json, updated_at, is_deleted)
            VALUES (?, ?, CURRENT_TIMESTAMP, ?)
            ON CONFLICT(list_name) DO UPDATE SET
                crdt_json = excluded.crdt_json,
                updated_at = excluded.updated_at,
                is_deleted = excluded.is_deleted
        )";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      return false;
    }

    // Serialize CRDT to JSON
    nlohmann::json j = list;
    string json_str = j.dump();

    sqlite3_bind_text(stmt, 1, list_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, json_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, is_deleted ? 1 : 0);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
  }

  /// Load a shopping list by name
  optional<CRDTShoppingList> load(const string &list_name) {
    if (!db_)
      return nullopt;

    const string sql =
        "SELECT crdt_json FROM shopping_lists WHERE list_name = ?";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      return nullopt;
    }

    sqlite3_bind_text(stmt, 1, list_name.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
      sqlite3_finalize(stmt);
      return nullopt;
    }

    const char *json_str =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    if (!json_str) {
      sqlite3_finalize(stmt);
      return nullopt;
    }

    try {
      nlohmann::json j = nlohmann::json::parse(json_str);
      CRDTShoppingList list = j.get<CRDTShoppingList>();
      sqlite3_finalize(stmt);
      return list;
    } catch (const exception &e) {
      sqlite3_finalize(stmt);
      return nullopt;
    }
  }

  /// List all shopping list names
  vector<string> list_all() {
    vector<string> names;
    if (!db_)
      return names;

    const string sql =
        "SELECT list_name FROM shopping_lists ORDER BY list_name";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      return names;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const char *name =
          reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
      if (name) {
        names.emplace_back(name);
      }
    }

    sqlite3_finalize(stmt);
    return names;
  }

  /// Delete a shopping list
  bool remove(const string &list_name) {
    if (!db_)
      return false;

    const string sql = "DELETE FROM shopping_lists WHERE list_name = ?";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      return false;
    }

    sqlite3_bind_text(stmt, 1, list_name.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
  }

  /// Count total number of keys
  size_t count() {
    if (!db_)
      return 0;

    const string sql = "SELECT COUNT(*) FROM shopping_lists";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
      return 0;

    size_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      count = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return count;
  }

  /// Get a list of keys with a limit
  vector<string> getKeys(int limit) {
    vector<string> keys;
    if (!db_)
      return keys;

    const string sql = "SELECT list_name FROM shopping_lists LIMIT ?";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
      return keys;

    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const char *name =
          reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
      if (name) {
        keys.emplace_back(name);
      }
    }
    sqlite3_finalize(stmt);
    return keys;
  }
};

#endif // NODE_STORAGE_H

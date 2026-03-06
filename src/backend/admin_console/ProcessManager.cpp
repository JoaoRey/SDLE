#include "ProcessManager.h"
#include "../common/Logger.h"
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std;

ProcessManager::ProcessManager() {}

ProcessManager::~ProcessManager() {
  // TODO: Kill all processes on destruction?
}

void ProcessManager::startRouter(const string &config_path, int port) {
  pid_t pid = fork();
  if (pid == 0) {
    // Child process
    setsid(); // Detach from terminal

    // Ensure logs directory exists
    struct stat st = {0};
    if (stat("logs", &st) == -1) {
      mkdir("logs", 0755);
    }

    // Redirect stdout/stderr to log file
    string log_file = "logs/" + to_string(getpid()) + ".log";
    int fd = open(log_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);

    // If opening log file fails, redirect to /dev/null to suppress output to
    // console
    if (fd == -1) {
      fd = open("/dev/null", O_WRONLY);
    }

    if (fd != -1) {
      dup2(fd, STDOUT_FILENO);
      dup2(fd, STDERR_FILENO);
      close(fd);
    }

    // Pass explicit config
    execl("./router_server", "router_server", "--config", config_path.c_str(),
          NULL);
    // If execl fails
    cerr << "Failed to start router_server" << endl;
    exit(1);
  } else if (pid > 0) {
    Logger::getInstance().ok("ProcessManager",
                             "Started Router with PID: " + to_string(pid));
    running_processes_[pid] = "Router";
  } else {
    Logger::getInstance().error("ProcessManager", "Fork failed");
  }
}

void ProcessManager::startNode(const string &config_path, int port,
                               const string &router_endpoint) {
  pid_t pid = fork();
  if (pid == 0) {
    // Child process
    setsid(); // Detach from terminal

    // Ensure logs directory exists
    struct stat st = {0};
    if (stat("logs", &st) == -1) {
      mkdir("logs", 0755);
    }

    // Redirect stdout/stderr to log file
    string log_file = "logs/" + to_string(getpid()) + ".log";
    int fd = open(log_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);

    // If opening log file fails, redirect to /dev/null to suppress output to
    // console
    if (fd == -1) {
      fd = open("/dev/null", O_WRONLY);
    }

    if (fd != -1) {
      dup2(fd, STDOUT_FILENO);
      dup2(fd, STDERR_FILENO);
      close(fd);
    }

    string port_str = to_string(port);
    // Const char* vector for args
    vector<const char *> args;
    args.push_back("node_server");
    args.push_back("--config");
    args.push_back(config_path.c_str());
    args.push_back("--port");
    args.push_back(port_str.c_str());
    if (!router_endpoint.empty()) {
      args.push_back("--router");
      args.push_back(router_endpoint.c_str());
    }
    args.push_back(NULL);

    execv("./node_server", (char *const *)args.data());

    cerr << "Failed to start node_server" << endl;
    exit(1);
  } else if (pid > 0) {
    Logger::getInstance().ok("ProcessManager",
                             "Started Node with PID: " + to_string(pid));
    running_processes_[pid] = "Node";
  } else {
    Logger::getInstance().error("ProcessManager", "Fork failed");
  }
}

void ProcessManager::stopProcess(int pid) {
  if (running_processes_.find(pid) == running_processes_.end()) {
    Logger::getInstance().warn("ProcessManager",
                               "Process " + to_string(pid) +
                                   " not found in managed processes.");
    return;
  }

  if (kill(pid, SIGTERM) == 0) {
    Logger::getInstance().ok("ProcessManager",
                             "Stopped process " + to_string(pid));
    running_processes_.erase(pid);
  } else {
    Logger::getInstance().error("ProcessManager",
                                "Failed to stop process " + to_string(pid));
  }
}

void ProcessManager::stopAllProcesses() {
  vector<int> nodes;
  vector<int> routers;

  // Classify processes
  for (const auto &pair : running_processes_) {
    if (pair.second == "Node") {
      nodes.push_back(pair.first);
    } else if (pair.second == "Router") {
      routers.push_back(pair.first);
    }
  }

  // Stop Nodes first
  for (int pid : nodes) {
    stopProcess(pid);
  }

  // Stop Routers next
  for (int pid : routers) {
    stopProcess(pid);
  }
}

void ProcessManager::listProcesses() const {
  Logger::getInstance().info("ProcessManager", "Managed Processes:");
  if (running_processes_.empty()) {
    cout << "  No processes started by this console." << endl;
    return;
  }

  for (const auto &pair : running_processes_) {
    cout << "  [" << pair.first << "] " << pair.second << endl;
  }
}

void ProcessManager::attachProcess(int pid) {
  if (running_processes_.find(pid) == running_processes_.end()) {
    Logger::getInstance().warn("ProcessManager",
                               "Process " + to_string(pid) +
                                   " not found in managed processes.");
    return;
  }

  string type = running_processes_.at(pid);
  string log_path = "logs/" + to_string(pid) + ".log";

  // Check if file exists
  struct stat buffer;
  if (stat(log_path.c_str(), &buffer) != 0) {
    Logger::getInstance().error("ProcessManager",
                                "Log file not found: " + log_path);
    return;
  }

  Logger::getInstance().info("ProcessManager", "Attaching to " + type + " (" +
                                                   to_string(pid) +
                                                   ") using 'less +F'.");
  cout << "Press Ctrl+C to stop following, then 'q' to exit." << endl;

  // Wait a bit for user to read message
  this_thread::sleep_for(chrono::milliseconds(1000));

  string command = "less +F " + log_path;
  int ret = system(command.c_str());
  if (ret != 0) {
    Logger::getInstance().error("ProcessManager", "Failed to execute less.");
  }
}

const std::map<int, std::string> &ProcessManager::getRunningProcesses() const {
  return running_processes_;
}

void ProcessManager::checkRunningProcesses() {
#ifdef NDEBUG
  Logger::getInstance().setState(LoggerState::SUPPRESS);
#endif

  int status;
  pid_t pid;

  // WNOHANG returns immediately if no child has exited.
  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    if (running_processes_.count(pid)) {
      string name = running_processes_[pid];
      running_processes_.erase(pid);

      string msg = "Process " + name + " (" + to_string(pid) + ") exited";
      if (WIFEXITED(status)) {
        msg += " with status " + to_string(WEXITSTATUS(status));
      } else if (WIFSIGNALED(status)) {
        msg += " due to signal " + to_string(WTERMSIG(status));
      }
      Logger::getInstance().warn("ProcessManager", msg);
    }
  }

#ifdef NDEBUG
  Logger::getInstance().setState(LoggerState::MINIMAL);
#endif
}

#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include <map>
#include <string>

class ProcessManager {
public:
  ProcessManager();
  ~ProcessManager();

  void startRouter(const std::string &config_path, int port);
  void startNode(const std::string &config_path, int port,
                 const std::string &router_endpoint = "");
  void stopProcess(int pid);
  void stopAllProcesses();
  void listProcesses() const;
  void attachProcess(int pid);
  void checkRunningProcesses();

  // Helper to get running processes for other uses if needed
  const std::map<int, std::string> &getRunningProcesses() const;

private:
  std::map<int, std::string> running_processes_;
};

#endif // PROCESS_MANAGER_H

#ifndef LOGGER_H
#define LOGGER_H

#include <mutex>
#include <string>

enum class LogLevel { OK, INFO, WARNING, ERROR };
enum class LoggerState { SUPPRESS, MINIMAL, FULL };

class Logger {
public:
  static Logger &getInstance();

  void setLevel(int level);

  void log(LogLevel level, const std::string &context,
           const std::string &message, int priority = 5);

  void setState(LoggerState state) { state_ = state; }

  // Helper methods for cleaner syntax
  void ok(const std::string &context, const std::string &message,
          int priority = 5);
  void info(const std::string &context, const std::string &message,
            int priority = 5);
  void warn(const std::string &context, const std::string &message,
            int priority = 5);
  void error(const std::string &context, const std::string &message,
             int priority = 5);

private:
  Logger() = default;
  ~Logger() = default;
  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;

  std::mutex mutex_;
  LoggerState state_ = LoggerState::FULL;
  int min_level_ = 5;

  std::string getLevelString(LogLevel level);
  std::string getColorCode(LogLevel level);
  std::string getResetColor();
  std::string getCurrentTime();
};

#endif // LOGGER_H

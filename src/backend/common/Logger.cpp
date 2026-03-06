#include "Logger.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

Logger &Logger::getInstance() {
  static Logger instance;
  return instance;
}

void Logger::setLevel(int level) {
  std::lock_guard<std::mutex> lock(mutex_);
  min_level_ = level;
}

void Logger::log(LogLevel level, const std::string &context,
                 const std::string &message, int priority) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (state_ == LoggerState::SUPPRESS)
    return;

  if (priority < min_level_)
    return;

  switch (state_) {
  case LoggerState::FULL:
    std::cout << getCurrentTime() << " " << context << " "
              << getColorCode(level) << getLevelString(level) << getResetColor()
              << "\t" << message << std::endl;
    break;
  case LoggerState::MINIMAL:
    std::cout << getColorCode(level) << getLevelString(level) << getResetColor()
              << "\t" << message << std::endl;
    break;
  case LoggerState::SUPPRESS:
    return;
  }
}

void Logger::ok(const std::string &context, const std::string &message,
                int priority) {
  log(LogLevel::OK, context, message, priority);
}

void Logger::info(const std::string &context, const std::string &message,
                  int priority) {
  log(LogLevel::INFO, context, message, priority);
}

void Logger::warn(const std::string &context, const std::string &message,
                  int priority) {
  log(LogLevel::WARNING, context, message, priority);
}

void Logger::error(const std::string &context, const std::string &message,
                   int priority) {
  log(LogLevel::ERROR, context, message, priority);
}

std::string Logger::getLevelString(LogLevel level) {
  switch (level) {
  case LogLevel::OK:
    return "OK";
  case LogLevel::INFO:
    return "INFO";
  case LogLevel::WARNING:
    return "WARNING";
  case LogLevel::ERROR:
    return "ERROR";
  default:
    return "UNKNOWN";
  }
}

std::string Logger::getColorCode(LogLevel level) {
  switch (level) {
  case LogLevel::OK:
    return "\033[32m"; // Green
  case LogLevel::INFO:
    return "\033[34m"; // Blue
  case LogLevel::WARNING:
    return "\033[33m"; // Yellow
  case LogLevel::ERROR:
    return "\033[31m"; // Red
  default:
    return "";
  }
}

std::string Logger::getResetColor() { return "\033[0m"; }

std::string Logger::getCurrentTime() {
  auto now = std::chrono::system_clock::now();
  auto in_time_t = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;

  std::stringstream ss;
  ss << std::put_time(std::localtime(&in_time_t), "%H:%M:%S") << "."
     << std::setfill('0') << std::setw(3) << ms.count();
  return ss.str();
}

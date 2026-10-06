#ifndef RUNTIME_STATE_HPP
#define RUNTIME_STATE_HPP

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

class MessageQueue;
enum class ServerLinkState
{
  DOWN,
  UP
};
enum class DataState
{
  PAUSED,
  RUNNING
};

struct RuntimeSnapshot
{
  ServerLinkState server_link = ServerLinkState::DOWN;
  DataState data_state = DataState::PAUSED;
  bool downstream_allowed = false;
  std::string pause_reason = "initializing";
  std::uint64_t epoch = 0;
  std::uint64_t session = 0;
  std::uint64_t discarded = 0;
  std::uint64_t reconnects = 0;
};

class RuntimeState
{
public:
  RuntimeSnapshot snapshot() const;
  std::uint64_t connected(MessageQueue& queue);
  void disconnected(MessageQueue& queue);
  void stop(MessageQueue& queue);
  bool control(std::uint64_t session, bool allowed,
               const std::string& reason, MessageQueue& queue);
  bool produce(std::uint64_t epoch, const std::function<void()>& operation);
  bool canSend(std::uint64_t epoch) const;
  void discard();

private:
  void pauseLocked(const std::string& reason, MessageQueue& queue);
  mutable std::mutex mutex_;
  RuntimeSnapshot state_;
  bool ever_connected_ = false;
};

#endif // RUNTIME_STATE_HPP

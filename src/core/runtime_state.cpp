#include "core/runtime_state.hpp"
#include "network/message_queue.hpp"

RuntimeSnapshot RuntimeState::snapshot() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

void RuntimeState::pauseLocked(const std::string& reason, MessageQueue& queue)
{
  state_.data_state = DataState::PAUSED;
  state_.downstream_allowed = false;
  state_.pause_reason = reason;
  ++state_.epoch;
  state_.discarded += queue.clear();
}

std::uint64_t RuntimeState::connected(MessageQueue& queue)
{
  std::lock_guard<std::mutex> lock(mutex_);
  pauseLocked("server_connection_restored", queue);
  state_.server_link = ServerLinkState::UP;
  ++state_.session;
  if (ever_connected_)
    ++state_.reconnects;
  ever_connected_ = true;
  return state_.session;
}

void RuntimeState::disconnected(MessageQueue& queue)
{
  std::lock_guard<std::mutex> lock(mutex_);
  state_.server_link = ServerLinkState::DOWN;
  pauseLocked("server_connection_lost", queue);
}

void RuntimeState::stop(MessageQueue& queue)
{
  std::lock_guard<std::mutex> lock(mutex_);
  state_.server_link = ServerLinkState::DOWN;
  pauseLocked("stopping", queue);
}

bool RuntimeState::control(std::uint64_t session, bool allowed,
                           const std::string& reason, MessageQueue& queue)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_.session != session || state_.server_link != ServerLinkState::UP)
    return false;
  if (!allowed)
    pauseLocked(reason, queue);
  else if (state_.data_state != DataState::RUNNING)
  {
    ++state_.epoch;
    state_.downstream_allowed = true;
    state_.data_state = DataState::RUNNING;
    state_.pause_reason.clear();
  }
  return true;
}

bool RuntimeState::produce(std::uint64_t epoch, const std::function<void()>& operation)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_.data_state != DataState::RUNNING || state_.epoch != epoch)
    return false;
  // Serialization and enqueue share the same boundary with pause + clear.
  operation();
  return true;
}

bool RuntimeState::canSend(std::uint64_t epoch) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return state_.data_state == DataState::RUNNING && state_.epoch == epoch;
}

void RuntimeState::discard()
{
  std::lock_guard<std::mutex> lock(mutex_);
  ++state_.discarded;
}

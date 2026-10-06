#include "network/network_worker.hpp"
#include "core/config.hpp"

#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>

NetworkWorker::NetworkWorker(MessageQueue& queue, TcpClient& client,
                             RuntimeState& state, Metrics& metrics)
  : queue_(queue), tcp_client_(client), runtime_state_(state), metrics_(metrics)
{
}

NetworkWorker::~NetworkWorker()
{
  stop();
}

void NetworkWorker::start()
{
  try
  {
    tx_thread_ = std::thread(&NetworkWorker::transmit, this);
    rx_thread_ = std::thread(&NetworkWorker::receive, this);
  }
  catch (...)
  {
    stop();
    throw;
  }
}

void NetworkWorker::stop()
{
  stopping_ = true;
  runtime_state_.stop(queue_);
  queue_.close();
  wake_.notify_all();
  tcp_client_.disconnect();
  if (tx_thread_.joinable())
    tx_thread_.join();
  if (rx_thread_.joinable())
    rx_thread_.join();
  tcp_client_.disconnect();
  runtime_state_.stop(queue_);
}

void NetworkWorker::lost()
{
  runtime_state_.disconnected(queue_);
  tcp_client_.disconnect();
  std::cerr << "Server Link DOWN: server_connection_lost\n";
}

void NetworkWorker::transmit()
{
  try
  {
    OutboundMessage message;
    while (queue_.pop(message))
    {
      std::lock_guard<std::mutex> gate(tx_gate_);
      if (stopping_)
        return;
      if (!runtime_state_.canSend(message.epoch))
      {
        runtime_state_.discard();
        continue;
      }
      // A frame admitted before PAUSE may already be in TCP; never replay it.
      if (!tcp_client_.sendData(message.payload))
      {
        runtime_state_.discard();
        lost();
        continue;
      }
      metrics_.recordSent();
    }
  }
  catch (const std::exception& error)
  {
    std::cerr << "Data TX error: " << error.what() << '\n';
    stopping_ = true;
    runtime_state_.stop(queue_);
    queue_.close();
    tcp_client_.disconnect();
    wake_.notify_all();
  }
}

void NetworkWorker::receive()
{
  try
  {
    while (!stopping_)
    {
      std::uint64_t session = 0;
      {
        std::lock_guard<std::mutex> gate(tx_gate_);
        if (stopping_)
          break;
        if (tcp_client_.connectToServer())
        {
          if (stopping_)
            break;
          session = runtime_state_.connected(queue_);
          std::cout << "Server Link UP: server_connection_restored; awaiting control\n";
        }
        else
          runtime_state_.disconnected(queue_);
      }
      if (session != 0)
      {
        std::string payload;
        while (!stopping_ && tcp_client_.receiveData(payload))
        {
          const auto json = nlohmann::json::parse(payload, nullptr, false);
          if (!json.is_object() || !json.contains("version") ||
              !json["version"].is_number_integer() || json["version"] != Config::PROTOCOL_VERSION ||
              !json.contains("type") || json["type"] != "control" ||
              !json.contains("device_id") || !json["device_id"].is_string() ||
              !json.contains("message_id") || !json["message_id"].is_string() ||
              !json.contains("data") || !json["data"].is_object())
          {
            std::cerr << "Ignoring invalid control envelope\n";
            continue;
          }
          const auto& data = json["data"];
          if (!data.contains("action") || !data["action"].is_string() ||
              !data.contains("reason") || !data["reason"].is_string() ||
              !data.contains("timestamp_ms") || !data["timestamp_ms"].is_number_integer())
          {
            std::cerr << "Ignoring invalid control data\n";
            continue;
          }
          const std::string action = data["action"].get<std::string>();
          const std::string reason = data["reason"].get<std::string>();
          if (action != "pause" && action != "resume")
          {
            std::cerr << "Ignoring unknown control action\n";
            continue;
          }
          // Resume waits for the previous admitted send to finish. Pause never
          // waits for network I/O, so capture/inference remain independent.
          std::unique_lock<std::mutex> gate(tx_gate_, std::defer_lock);
          if (action == "resume")
            gate.lock();
          if (!stopping_ && runtime_state_.control(session, action == "resume", reason, queue_))
            std::cout << "Control " << action << ": " << reason << '\n';
        }
        if (!stopping_)
          runtime_state_.disconnected(queue_);
        std::lock_guard<std::mutex> gate(tx_gate_);
        if (!stopping_)
          lost();
      }
      std::unique_lock<std::mutex> wait(wait_mutex_);
      wake_.wait_for(wait, std::chrono::milliseconds(Config::RECONNECT_DELAY_MS),
                     [this] { return stopping_.load(); });
    }
  }
  catch (const std::exception& error)
  {
    std::cerr << "Control RX error: " << error.what() << '\n';
    stopping_ = true;
    runtime_state_.stop(queue_);
    queue_.close();
    tcp_client_.disconnect();
  }
}

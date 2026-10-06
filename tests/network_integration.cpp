#include "network/network_worker.hpp"
#include "protocol/serializer.hpp"
#include "core/message_id.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <nlohmann/json.hpp>

namespace
{
void require(bool ok, const char* message)
{
  if (!ok)
    throw std::runtime_error(message);
}
void waitFor(const std::function<bool()>& predicate)
{
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!predicate())
  {
    require(std::chrono::steady_clock::now() < end, "state transition timeout");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}
struct Socket
{
  int fd;
  ~Socket() { if (fd >= 0) close(fd); }
};
void sendBytes(int fd, const std::string& bytes, bool fragmented = false)
{
  std::size_t offset = 0;
  while (offset < bytes.size())
  {
    const ssize_t n = send(fd, bytes.data() + offset,
                          fragmented ? 1 : bytes.size() - offset, MSG_NOSIGNAL);
    require(n > 0, "gateway send failed");
    offset += n;
    if (fragmented)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
void framed(int fd, const std::string& payload, bool fragmented = false)
{
  std::uint32_t length = htonl(payload.size());
  sendBytes(fd, std::string(reinterpret_cast<char*>(&length), 4) + payload, fragmented);
}
std::string control(const std::string& action, const std::string& reason)
{
  return nlohmann::json{{"version", 1}, {"type", "control"},
    {"device_id", "gateway"}, {"message_id", "control-1"},
    {"data", {{"timestamp_ms", 1}, {"action", action}, {"reason", reason}}}}.dump();
}
std::string readBytes(int fd, std::size_t size)
{
  std::string bytes(size, '\0');
  std::size_t offset = 0;
  while (offset < size)
  {
    pollfd p{fd, POLLIN, 0};
    require(poll(&p, 1, 3000) > 0, "gateway receive timeout");
    ssize_t n = recv(fd, &bytes[offset], size - offset, 0);
    require(n > 0, "gateway receive failed");
    offset += n;
  }
  return bytes;
}
std::string readFrame(int fd)
{
  auto prefix = readBytes(fd, 4);
  std::uint32_t size;
  std::memcpy(&size, prefix.data(), 4);
  size = ntohl(size);
  require(size > 0 && size <= 1024 * 1024, "invalid vision framing");
  return readBytes(fd, size);
}
int acceptClient(int listener)
{
  pollfd p{listener, POLLIN, 0};
  require(poll(&p, 1, 5000) > 0, "reconnect missing");
  int fd = accept(listener, nullptr, nullptr);
  require(fd >= 0, "accept failed");
  return fd;
}
void noFrame(int fd)
{
  pollfd p{fd, POLLIN, 0};
  require(poll(&p, 1, 100) == 0, "unexpected stale vision");
}
}

int main()
{
  try
  {
    MessageQueue queue(16);
    RuntimeState state;
    Metrics metrics;
    Socket listener{socket(AF_INET, SOCK_STREAM, 0)};
    require(listener.fd >= 0, "socket failed");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    require(bind(listener.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "bind failed");
    socklen_t size = sizeof(address);
    require(getsockname(listener.fd, reinterpret_cast<sockaddr*>(&address), &size) == 0, "getsockname failed");
    TcpClient client("127.0.0.1", ntohs(address.sin_port));
    NetworkWorker worker(queue, client, state, metrics);
    worker.start();
    // Initial connection refusal must recover even with an empty PAUSED queue.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    require(listen(listener.fd, 4) == 0, "listen failed");
    Socket peer{acceptClient(listener.fd)};
    waitFor([&] { return state.snapshot().server_link == ServerLinkState::UP; });
    require(state.snapshot().data_state == DataState::PAUSED, "connect auto-resumed");
    noFrame(peer.fd);
    framed(peer.fd, "[]");
    framed(peer.fd, "{bad json");
    framed(peer.fd, R"({"version":"bad"})");
    framed(peer.fd, control("unknown", "ignored"));
    noFrame(peer.fd);
    framed(peer.fd, control("resume", "wsl_connection_restored"), true);
    waitFor([&] { return state.snapshot().data_state == DataState::RUNNING; });
    auto old = state.snapshot();
    Serializer serializer;
    Detection detection{2, "car", 0.9f, {10, 20, 30, 40}};
    for (int i = 1; i <= 40; ++i)
    {
      auto id = createMessageId(1, i);
      require(state.produce(old.epoch, [&]
      {
        queue.push({id, serializer.serialize({1, 1234}, detection, id), old.epoch});
        metrics.recordProduced();
      }), "running producer rejected");
      // No ACK is ever sent back.
      auto message = nlohmann::json::parse(readFrame(peer.fd));
      require(message["message_id"] == id && message["data"]["timestamp_ms"] == 1234,
              "vision semantics changed");
      require(message["data"]["class_id"] == 2 &&
              message["data"]["bbox"]["y"] == 20 &&
              !message["data"].contains("detections"), "object vision schema");
    }
    // Control silence longer than the removed ACK timeout is normal.
    std::this_thread::sleep_for(std::chrono::milliseconds(1700));
    require(state.snapshot().data_state == DataState::RUNNING, "idle control disconnected");
    framed(peer.fd, control("pause", "database_write_failed"));
    waitFor([&] { return state.snapshot().data_state == DataState::PAUSED; });
    require(state.snapshot().pause_reason == "database_write_failed", "reason changed");
    bool called = false;
    require(!state.produce(old.epoch, [&] { called = true; }), "paused producer accepted");
    require(!called && queue.size() == 0, "paused JSON/queue work occurred");
    framed(peer.fd, control("resume", "database_recovered"));
    waitFor([&] { return state.snapshot().data_state == DataState::RUNNING; });
    require(!state.produce(old.epoch, [] {}), "old frame survived resume");
    noFrame(peer.fd);
    const auto old_session = state.snapshot().session;
    shutdown(peer.fd, SHUT_RDWR);
    close(peer.fd);
    peer.fd = -1;
    waitFor([&] { return state.snapshot().server_link == ServerLinkState::DOWN; });
    require(state.snapshot().pause_reason == "server_connection_lost", "wrong lost reason");
    peer.fd = acceptClient(listener.fd);
    waitFor([&] { return state.snapshot().session > old_session; });
    require(state.snapshot().data_state == DataState::PAUSED, "reconnect auto-resumed");
    require(state.snapshot().pause_reason == "server_connection_restored", "wrong restored reason");
    require(!state.control(old_session, true, "stale", queue), "old session control accepted");
    noFrame(peer.fd);
    framed(peer.fd, control("resume", "wsl_connection_restored"));
    waitFor([&] { return state.snapshot().data_state == DataState::RUNNING; });
    auto current = state.snapshot();
    require(state.produce(current.epoch, [&] { queue.push({"new", "new", current.epoch}); }), "recovery failed");
    require(readFrame(peer.fd) == "new", "post-reconnect send failed");
    // A peer which stops reading must cause TX failure -> PAUSE/CLEAR,
    // while the independent RX owner reconnects without replaying the frame.
    int receive_buffer = 1024;
    require(setsockopt(peer.fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer, sizeof(receive_buffer)) == 0,
            "receive buffer setup");
    const auto before_failure = state.snapshot();
    state.produce(before_failure.epoch, [&]
    {
      for (int i = 0; i < 8; ++i)
        queue.push({"blocked", std::string(1024 * 1024, 'x'), before_failure.epoch});
    });
    waitFor([&] { return state.snapshot().server_link == ServerLinkState::DOWN; });
    require(queue.size() == 0 && state.snapshot().discarded > 0, "TX failure did not clear queue");
    close(peer.fd);
    peer.fd = acceptClient(listener.fd);
    waitFor([&] { return state.snapshot().session > before_failure.session; });
    noFrame(peer.fd);
    // Shutdown must wake a receive blocked part-way through a prefix.
    sendBytes(peer.fd, std::string(2, '\0'));
    const auto start = std::chrono::steady_clock::now();
    worker.stop();
    require(std::chrono::steady_clock::now() - start < std::chrono::seconds(3), "shutdown hung");

    // Queue overflow and pause discard have different counters.
    MessageQueue bounded(2);
    RuntimeState local;
    auto session = local.connected(bounded);
    require(local.control(session, true, "ready", bounded), "local resume failed");
    auto epoch = local.snapshot().epoch;
    local.produce(epoch, [&]
    {
      bounded.push({"1", "1", epoch});
      bounded.push({"2", "2", epoch});
      bounded.push({"3", "3", epoch});
    });
    local.control(session, false, "queue_overload", bounded);
    require(bounded.droppedCount() == 1 && local.snapshot().discarded == 2 && bounded.size() == 0,
            "drop accounting wrong");
    // Stress the producer / pause boundary concurrently.
    std::atomic<bool> done{false};
    std::thread producer([&]
    {
      while (!done)
      {
        auto s = local.snapshot();
        local.produce(s.epoch, [&] { bounded.push({"race", "race", s.epoch}); });
        std::this_thread::yield();
      }
    });
    for (int i = 0; i < 100; ++i)
    {
      local.control(session, true, "ready", bounded);
      local.control(session, false, "pause", bounded);
      if (bounded.size() != 0)
      {
        done = true;
        producer.join();
        throw std::runtime_error("enqueue after pause clear");
      }
    }
    done = true;
    producer.join();
    std::cout << "PASS: network integration, framing, control, recovery, queue race, shutdown\n";
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}

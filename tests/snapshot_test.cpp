#include "protocol/snapshot_publisher.hpp"
#include "protocol/serializer.hpp"
#include "core/metrics.hpp"
#include "network/message_queue.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>
#include <set>

namespace
{
void require(bool ok, const char* message)
{
  if (!ok)
    throw std::runtime_error(message);
}
}

int main()
{
  try
  {
    Serializer serializer;
    MessageQueue queue(64);
    RuntimeState state;
    Metrics metrics;
    SnapshotPublisher publisher(serializer, queue, state, metrics, 42);
    const auto start = SnapshotPublisher::Clock::time_point{};
    std::vector<Detection> detections(20, {2, "car", 0.9f, {10, 20, 30, 40}});
    for (std::size_t i = 0; i < detections.size(); ++i)
      detections[i].bbox.x += static_cast<int>(i);
    require(publisher.publish({1, 1000}, detections, state.snapshot(), start), "paused publish");
    require(queue.size() == 0, "initial PAUSE generated data");
    auto session = state.connected(queue);
    state.control(session, true, "ready", queue);
    auto frame = state.snapshot();
    publisher.publish({2, 2000}, detections, frame, start);
    publisher.publish({3, 3000}, detections, frame, start + std::chrono::milliseconds(999));
    require(queue.size() == 0, "sent before one second");
    publisher.publish({4, 4000}, detections, frame, start + std::chrono::seconds(1));
    require(queue.size() == 21, "twenty objects must generate 21 messages");
    std::set<std::string> ids;
    std::uint64_t sequence = 0;
    auto checkPair = [&](std::uint64_t frame_id, std::int64_t timestamp, std::size_t count)
    {
      require(queue.size() == count + 1, "snapshot message count mismatch");
      for (std::size_t i = 0; i <= count; ++i)
      {
        OutboundMessage message;
        require(queue.pop(message), "snapshot message missing");
        const auto json = nlohmann::json::parse(message.payload);
        const auto& data = json["data"];
        require(json["version"] == 1 && json["device_id"] == "vision-pi-01", "envelope");
        require(data["frame_id"] == frame_id && data["timestamp_ms"] == timestamp,
                "frame/timestamp mismatch");
        require(!data.contains("detections") && !data.contains("track_id"), "unexpected array/track_id");
        require(json["message_id"] == message.message_id && ids.insert(message.message_id).second,
                "duplicate/mismatched ID");
        require(std::stoull(message.message_id.substr(message.message_id.rfind('-') + 1)) == ++sequence,
                "message sequence mismatch");
        if (i < count)
        {
          require(json["type"] == "vision" && data["class_id"] == 2 &&
                  data["class_name"] == "car" && data["confidence"] == 0.9f,
                  "object fields mismatch");
          require(data["bbox"] == nlohmann::json({{"x", 10 + static_cast<int>(i)}, {"y", 20}, {"width", 30}, {"height", 40}}),
                  "bbox mismatch");
          require(!data.contains("vehicle_count"), "count in vision");
        }
        else
          require(json["type"] == "vehicle_count" && data["vehicle_count"] == count,
                  "vehicle count mismatch");
      }
    };
    checkPair(4, 4000, 20);
    publisher.publish({5, 5000}, {}, frame, start + std::chrono::seconds(2));
    checkPair(5, 5000, 0);
    // A slow frame produces one fresh snapshot, never a catch-up burst.
    publisher.publish({6, 6000}, detections, frame, start + std::chrono::seconds(12));
    require(queue.size() == 21, "catch-up burst");
    checkPair(6, 6000, 20);
    publisher.publish({7, 7000}, detections, frame, start + std::chrono::seconds(12));
    require(queue.size() == 0, "repeat at same time");
    state.control(session, false, "pause", queue);
    publisher.publish({8, 8000}, detections, frame, start + std::chrono::seconds(13));
    require(queue.size() == 0, "stale frame crossed PAUSE");
    publisher.publish({9, 9000}, {}, state.snapshot(), start + std::chrono::seconds(22));
    state.control(session, true, "ready", queue);
    publisher.publish({10, 10000}, detections, frame, start + std::chrono::seconds(23));
    require(queue.size() == 0, "old epoch survived RESUME");
    frame = state.snapshot();
    publisher.publish({11, 11000}, detections, frame, start + std::chrono::seconds(24));
    require(queue.size() == 0, "RESUME replay");
    publisher.publish({12, 12000}, {}, frame, start + std::chrono::seconds(25));
    checkPair(12, 12000, 0);
    publisher.publish({13, 13000}, detections, frame, start + std::chrono::seconds(26));
    require(queue.size() == 21, "queued pair missing");
    state.control(session, false, "pause", queue);
    require(queue.size() == 0, "PAUSE did not clear both types");
    std::cout << "PASS: one-second snapshots, all/zero detections, matching frames/timestamps, unique IDs, PAUSE/RESUME\n";
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}

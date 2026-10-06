#ifndef SNAPSHOT_PUBLISHER_HPP
#define SNAPSHOT_PUBLISHER_HPP

#include <chrono>
#include <cstdint>
#include <vector>
#include "core/detection_result.hpp"
#include "core/runtime_state.hpp"

class Serializer;
class MessageQueue;
class Metrics;

class SnapshotPublisher
{
public:
  using Clock = std::chrono::steady_clock;
  SnapshotPublisher(Serializer& serializer, MessageQueue& queue,
                    RuntimeState& state, Metrics& metrics, std::uint64_t boot_id);
  bool publish(const DetectionResult& result, const std::vector<Detection>& detections,
               const RuntimeSnapshot& frame_state, Clock::time_point now = Clock::now());

private:
  Serializer& serializer_;
  MessageQueue& queue_;
  RuntimeState& state_;
  Metrics& metrics_;
  std::uint64_t boot_id_;
  std::uint64_t sequence_ = 0;
  std::uint64_t epoch_ = 0;
  bool active_ = false;
  Clock::time_point last_snapshot_{};
};

#endif // SNAPSHOT_PUBLISHER_HPP

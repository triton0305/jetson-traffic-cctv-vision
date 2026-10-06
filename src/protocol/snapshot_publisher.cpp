#include "protocol/snapshot_publisher.hpp"
#include "protocol/serializer.hpp"
#include "network/message_queue.hpp"
#include "core/message_id.hpp"
#include "core/metrics.hpp"

SnapshotPublisher::SnapshotPublisher(Serializer& serializer, MessageQueue& queue,
                                     RuntimeState& state, Metrics& metrics, std::uint64_t boot_id)
  : serializer_(serializer), queue_(queue), state_(state), metrics_(metrics), boot_id_(boot_id)
{
}

bool SnapshotPublisher::publish(const DetectionResult& result,
                                const std::vector<Detection>& detections,
                                const RuntimeSnapshot& frame_state, Clock::time_point now)
{
  if (frame_state.data_state != DataState::RUNNING)
  {
    active_ = false;
    return true;
  }
  if (!active_ || epoch_ != frame_state.epoch)
  {
    active_ = true;
    epoch_ = frame_state.epoch;
    last_snapshot_ = now;
    return true;
  }
  if (now - last_snapshot_ < std::chrono::seconds(1))
    return true;
  bool healthy = true;
  const bool admitted = state_.produce(frame_state.epoch, [&]
  {
    for (const auto& detection : detections)
    {
      const auto vision_id = createMessageId(boot_id_, ++sequence_);
      const auto vision = serializer_.serialize(result, detection, vision_id);
      if (!queue_.push({vision_id, vision, frame_state.epoch}))
      {
        healthy = false;
        return;
      }
      metrics_.recordProduced();
    }
    const auto count_id = createMessageId(boot_id_, ++sequence_);
    const auto count = serializer_.serializeVehicleCount(result, detections.size(), count_id);
    if (!queue_.push({count_id, count, frame_state.epoch}))
    {
      healthy = false;
      return;
    }
    metrics_.recordProduced();
  });
  // No catch-up or cached frames after a pause or a slow inference.
  last_snapshot_ = now;
  if (!admitted)
    active_ = false;
  return healthy;
}

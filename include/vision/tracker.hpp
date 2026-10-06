#ifndef TRACKER_HPP
#define TRACKER_HPP

#include "core/detection_result.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

struct TrackedDetection
{
  Detection detection;
  std::uint64_t track_id;
};

class Tracker
{
public:
  using Clock = std::chrono::steady_clock;

  static constexpr float MIN_IOU = 0.10f;
  static constexpr double MAX_CENTER_DISTANCE = 160.0;
  static constexpr int TRACK_TIMEOUT_MS = 800;

  std::vector<TrackedDetection> update(
    const std::vector<Detection>& detections, Clock::time_point now = Clock::now());
  std::size_t retainedTrackCount() const;

private:
  struct Track
  {
    std::uint64_t track_id;
    int class_id;
    BoundingBox bbox;
    Clock::time_point last_seen;
    std::uint64_t missed_frames;
  };

  std::vector<Track> tracks_;
  std::uint64_t next_track_id_ = 1;
};

#endif // TRACKER_HPP

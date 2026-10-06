#include "vision/tracker.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
float calculateIoU(const BoundingBox& a, const BoundingBox& b)
{
  const int left = std::max(a.x, b.x);
  const int top = std::max(a.y, b.y);
  const int right = std::min(a.x + a.width, b.x + b.width);
  const int bottom = std::min(a.y + a.height, b.y + b.height);
  const double intersection = static_cast<double>(std::max(0, right - left)) *
    std::max(0, bottom - top);
  const double union_area = static_cast<double>(a.width) * a.height +
    static_cast<double>(b.width) * b.height - intersection;
  return union_area > 0 ? static_cast<float>(intersection / union_area) : 0.0f;
}

double calculateCenterDistance(const BoundingBox& a, const BoundingBox& b)
{
  return std::hypot(a.x + a.width / 2.0 - b.x - b.width / 2.0,
                    a.y + a.height / 2.0 - b.y - b.height / 2.0);
}
}

std::vector<TrackedDetection> Tracker::update(
  const std::vector<Detection>& detections, Clock::time_point now)
{
  // Expire before association so an old ID cannot be revived at the boundary.
  tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [&](const Track& track)
  {
    return now - track.last_seen >= std::chrono::milliseconds(TRACK_TIMEOUT_MS);
  }), tracks_.end());

  std::vector<bool> matched(tracks_.size(), false);
  std::vector<TrackedDetection> results;
  results.reserve(detections.size());

  for (const Detection& detection : detections)
  {
    std::size_t best = tracks_.size();
    float best_iou = -1.0f;
    double best_distance = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < tracks_.size(); ++i)
    {
      if (matched[i] || tracks_[i].class_id != detection.class_id)
        continue;
      const float iou = calculateIoU(tracks_[i].bbox, detection.bbox);
      const double distance = calculateCenterDistance(tracks_[i].bbox, detection.bbox);
      if (iou < MIN_IOU && distance > MAX_CENTER_DISTANCE)
        continue;
      // Exact ties retain the earlier (lower ID) track, as in the old tracker.
      if (iou > best_iou || (iou == best_iou && distance < best_distance))
      {
        best = i;
        best_iou = iou;
        best_distance = distance;
      }
    }

    if (best == tracks_.size())
    {
      if (next_track_id_ == 0)
        throw std::overflow_error("Track ID exhausted");
      tracks_.push_back({next_track_id_++, detection.class_id, detection.bbox, now, 0});
      matched.push_back(true);
      results.push_back({detection, tracks_.back().track_id});
    }
    else
    {
      Track& track = tracks_[best];
      track.bbox = detection.bbox;
      track.last_seen = now;
      track.missed_frames = 0;
      matched[best] = true;
      results.push_back({detection, track.track_id});
    }
  }

  for (std::size_t i = 0; i < tracks_.size(); ++i)
  {
    if (!matched[i] && tracks_[i].missed_frames != std::numeric_limits<std::uint64_t>::max())
      ++tracks_[i].missed_frames;
  }
  return results;
}

std::size_t Tracker::retainedTrackCount() const
{
  return tracks_.size();
}

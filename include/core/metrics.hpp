#ifndef METRICS_HPP
#define METRICS_HPP

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include "core/runtime_state.hpp"

struct MetricsSnapshot
{
  double effective_fps = 0;
  double avg_inference_ms = 0;
};

class Metrics
{
public:
  void recordFrame(double inference_ms, std::size_t queue_size,
                   std::uint64_t dropped, const RuntimeSnapshot& state);
  void recordProduced();
  void recordSent();
  MetricsSnapshot snapshot() const;

private:
  std::chrono::steady_clock::time_point last_report_time_ = std::chrono::steady_clock::now();
  std::uint64_t frames_ = 0;
  std::uint64_t produced_ = 0;
  std::uint64_t sent_ = 0;
  double inference_ms_ = 0;
  MetricsSnapshot last_report_;
  mutable std::mutex mutex_;
};

#endif // METRICS_HPP

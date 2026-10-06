#include "core/metrics.hpp"
#include <iostream>

void Metrics::recordFrame(double inference_ms, std::size_t queue_size,
                          std::uint64_t dropped, const RuntimeSnapshot& state)
{
  std::lock_guard<std::mutex> lock(mutex_);
  ++frames_;
  inference_ms_ += inference_ms;
  const auto now = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(now - last_report_time_).count();
  if (seconds < 1.0)
    return;
  last_report_ = {frames_ / seconds, inference_ms_ / frames_};
  std::cout << "Metrics: Effective FPS=" << frames_ / seconds
            << " | Avg Inference=" << inference_ms_ / frames_ << " ms"
            << " | Produced msg/s=" << produced_ / seconds
            << " | Sent msg/s=" << sent_ / seconds
            << " | Queue=" << queue_size
            << " | Queue overflow dropped=" << dropped
            << " | Discarded on PAUSE=" << state.discarded
            << " | Network State=" << (state.data_state == DataState::RUNNING ? "RUNNING" : "PAUSED")
            << " | Server Link=" << (state.server_link == ServerLinkState::UP ? "UP" : "DOWN")
            << " | Pause Reason=" << state.pause_reason
            << " | Reconnect count=" << state.reconnects << '\n';
  last_report_time_ = now;
  frames_ = produced_ = sent_ = 0;
  inference_ms_ = 0;
}

void Metrics::recordProduced()
{
  std::lock_guard<std::mutex> lock(mutex_);
  ++produced_;
}

void Metrics::recordSent()
{
  std::lock_guard<std::mutex> lock(mutex_);
  ++sent_;
}

MetricsSnapshot Metrics::snapshot() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return last_report_;
}

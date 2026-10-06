#include "vision/vision_worker.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include "core/detection_result.hpp"
#include "core/message_id.hpp"
#include "core/metrics.hpp"
#include "core/runtime_state.hpp"

#include "network/message_queue.hpp"

#include "protocol/outbound_message.hpp"
#include "protocol/serializer.hpp"
#include "protocol/snapshot_publisher.hpp"

#include "input/cctv_stream.hpp"
#include "vision/detector.hpp"
#include "vision/postprocessor.hpp"
#include "vision/preprocessor.hpp"
#include "vision/tracker.hpp"

namespace
{
std::int64_t currentUnixTimeMs()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
}
}

VisionWorker::VisionWorker(
  CctvStream& camera,
  Preprocessor& preprocessor,
  Detector& detector,
  PostProcessor& postprocessor,
  Tracker& tracker,
  Serializer& serializer,
  MessageQueue& message_queue,
  RuntimeState& runtime_state,
  Metrics& metrics,
  std::uint64_t boot_id,
  volatile std::sig_atomic_t& running)
  : camera_(camera),
    preprocessor_(preprocessor),
    detector_(detector),
    postprocessor_(postprocessor),
    tracker_(tracker),
    serializer_(serializer),
    message_queue_(message_queue),
    runtime_state_(runtime_state),
    metrics_(metrics),
    boot_id_(boot_id),
    running_(running)
{
}

void VisionWorker::run()
{
  std::uint64_t frame_id = 0;
  SnapshotPublisher publisher(serializer_, message_queue_, runtime_state_, metrics_, boot_id_);
  std::cout << "Edge Vision loop started\n";
  std::cout << "Boot ID: " << boot_id_ << '\n';
  std::cout << "Press Ctrl+C to quit\n";

  while (running_)
  {
    // Capture the admission epoch before capture/inference. A frame spanning
    // PAUSE/RESUME is never treated as a new post-resume detection.
    const auto frame_state = runtime_state_.snapshot();

    cv::Mat frame;

    if (!camera_.read(frame, running_))
    {
      std::cerr << "Failed to capture frame\n";
      break;
    }

    const std::uint64_t current_frame_id = frame_id++;
    const std::int64_t timestamp_ms = currentUnixTimeMs();

    cv::Mat blob = preprocessor_.process(frame);

    const auto inference_start = std::chrono::steady_clock::now();
    std::vector<cv::Mat> outputs = detector_.infer(blob);
    const auto inference_end = std::chrono::steady_clock::now();

    const double inference_ms =
      std::chrono::duration<double, std::milli>(
        inference_end - inference_start).count();

    std::vector<Detection> detections = postprocessor_.process(
      outputs,
      frame.cols,
      frame.rows,
      preprocessor_.inputWidth(),
      preprocessor_.inputHeight());

    const std::vector<TrackedDetection> tracked_detections = tracker_.update(detections);
    cv::Mat display_frame = frame.clone();

    for (const TrackedDetection& tracked_detection : tracked_detections)
    {
      const Detection& detection = tracked_detection.detection;
      const BoundingBox& bbox = detection.bbox;

      cv::rectangle(
        display_frame,
        cv::Rect(bbox.x, bbox.y, bbox.width, bbox.height),
        cv::Scalar(0, 255, 0),
        2);

      std::ostringstream label;
      label << detection.class_name << " ID:" << tracked_detection.track_id << ' '
            << std::fixed << std::setprecision(2)
            << detection.confidence;

      const int label_y = bbox.y > 20 ? bbox.y - 8 : bbox.y + 20;

      cv::putText(
        display_frame,
        label.str(),
        cv::Point(bbox.x, label_y),
        cv::FONT_HERSHEY_SIMPLEX,
        0.6,
        cv::Scalar(0, 255, 0),
        2);
    }

    const MetricsSnapshot display_metrics = metrics_.snapshot();
    std::ostringstream fps_label;
    fps_label << "FPS: " << std::fixed << std::setprecision(1) << display_metrics.effective_fps;
    std::ostringstream inference_label;
    inference_label << "Inference: " << std::fixed << std::setprecision(1)
                    << display_metrics.avg_inference_ms << " ms";
    const std::string overlay[] = {
      fps_label.str(), inference_label.str(),
      "Active Tracks: " + std::to_string(tracked_detections.size())};
    for (int i = 0; i < 3; ++i)
    {
      const cv::Point position(10, 25 + i * 24);
      cv::putText(display_frame, overlay[i], position, cv::FONT_HERSHEY_SIMPLEX,
                  0.6, cv::Scalar(0, 0, 0), 3);
      cv::putText(display_frame, overlay[i], position, cv::FONT_HERSHEY_SIMPLEX,
                  0.6, cv::Scalar(255, 255, 255), 1);
    }

    cv::imshow("Edge Vision", display_frame);

    if (cv::waitKey(1) == 27)
    {
      running_ = 0;
    }

    if (running_ && !publisher.publish({current_frame_id, timestamp_ms}, detections, frame_state))
    {
      std::cerr << "Failed to enqueue snapshot\n";
      running_ = 0;
    }

    metrics_.recordFrame(
      inference_ms,
      message_queue_.size(),
      message_queue_.droppedCount(),
      runtime_state_.snapshot());
  }

}

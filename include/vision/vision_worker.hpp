#ifndef VISION_WORKER_HPP
#define VISION_WORKER_HPP

#include <csignal>
#include <cstdint>

class CctvStream;
class Preprocessor;
class Detector;
class PostProcessor;
class Tracker;
class Serializer;
class MessageQueue;
class Metrics;
class RuntimeState;

class VisionWorker
{
public:
  VisionWorker(
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
    volatile std::sig_atomic_t& running);

  void run();

private:
  CctvStream& camera_;
  Preprocessor& preprocessor_;
  Detector& detector_;
  PostProcessor& postprocessor_;
  Tracker& tracker_;
  Serializer& serializer_;
  MessageQueue& message_queue_;
  RuntimeState& runtime_state_;
  Metrics& metrics_;
  std::uint64_t boot_id_;
  volatile std::sig_atomic_t& running_;
};

#endif // VISION_WORKER_HPP

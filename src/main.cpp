#include <csignal>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <iostream>
#include <string>
#include <opencv2/highgui.hpp>

#include "core/boot_id.hpp"
#include "core/config.hpp"
#include "core/metrics.hpp"
#include "core/runtime_state.hpp"

#include "input/utic_cctv_provider.hpp"
#include "input/cctv_stream.hpp"
#include "vision/detector.hpp"
#include "vision/postprocessor.hpp"
#include "vision/preprocessor.hpp"
#include "vision/vision_worker.hpp"
#include "vision/tracker.hpp"

#include "network/message_queue.hpp"
#include "network/network_worker.hpp"
#include "network/tcp_client.hpp"

#include "protocol/serializer.hpp"

volatile std::sig_atomic_t running = 1;

void handleSignal(int)
{
  running = 0;
}

int runApplication(int argc, char* argv[])
{
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  if (argc != 3)
  {
    std::cerr << "Usage: " << argv[0] << " <server_ip> <server_port>\n";
    return 1;
  }

  const std::string server_ip = argv[1];
  int server_port = 0;

  try
  {
    std::size_t consumed = 0;
    const std::string port_text = argv[2];
    server_port = std::stoi(port_text, &consumed);
    if (consumed != port_text.size())
      throw std::invalid_argument("Trailing characters in server port");
  }
  catch (const std::exception&)
  {
    std::cerr << "Invalid server port\n";
    return 1;
  }

  if (server_port < 1 || server_port > 65535)
  {
    std::cerr << "Server port must be between 1 and 65535\n";
    return 1;
  }

  const std::string model_path = MODEL_PATH;

  UticCctvProvider provider(UticCctvConfig::fromEnvironment());
  const auto endpoint = provider.select();
  CctvStream camera(endpoint.stream_url, [&] { return provider.select(true).stream_url; });
  Preprocessor preprocessor(640, 640);
  Detector detector(model_path);
  PostProcessor postprocessor(0.25f, 0.45f);
  Tracker tracker;
  Serializer serializer;
  TcpClient tcp_client(server_ip, server_port);
  Metrics metrics;
  RuntimeState runtime_state;
  MessageQueue message_queue(Config::MAX_QUEUE_SIZE);
  NetworkWorker network_worker(
    message_queue, tcp_client, runtime_state, metrics);

  if (!camera.open())
  {
    std::cerr << "Failed to open CCTV Stream\n";
    return 1;
  }

  if (!detector.isLoaded())
  {
    std::cerr << "Failed to load model\n";
    return 1;
  }

  std::uint64_t boot_id = 0;

  if (!loadAndIncrementBootId(boot_id))
  {
    std::cerr << "Failed to initialize boot_id\n";
    return 1;
  }

  VisionWorker vision_worker(
    camera, preprocessor, detector, postprocessor, tracker,
    serializer, message_queue, runtime_state, metrics,
    boot_id, running);

  int exit_code = 0;
  try
  {
    network_worker.start();
    vision_worker.run();
  }
  catch (const std::exception& error)
  {
    std::cerr << "Runtime error: " << error.what() << '\n';
    exit_code = 1;
  }
  network_worker.stop();
  camera.release();
  cv::destroyAllWindows();

  std::cout << "Edge Vision loop stopped\n";

  return exit_code;
}

int main(int argc, char* argv[])
{
  try
  {
    return runApplication(argc, argv);
  }
  catch (const std::exception& error)
  {
    std::cerr << "Initialization error: " << error.what() << '\n';
    return 1;
  }
}

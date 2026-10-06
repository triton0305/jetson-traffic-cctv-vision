#include "input/cctv_provider.hpp"
#include "input/cctv_stream.hpp"

#include <ctime>
#include <iostream>
#include <stdexcept>

// Skip cooldown sleeps only in this executable; production keeps its backoff.
extern "C" int __wrap_nanosleep(const timespec*, timespec*)
{
  return 0;
}

int main()
{
  volatile std::sig_atomic_t running = 1;
  cv::Mat frame;
  int attempts = 0;
  CctvStream stream("/nonexistent-cctv-recovery-test", [&]() -> std::string
  {
    if (++attempts == 1)
      throw CctvTransientError("simulated timeout");
    throw std::runtime_error("simulated refresh limit");
  });
  try
  {
    stream.read(frame, running);
    return 1;
  }
  catch (const CctvTransientError&)
  {
    return 2;
  }
  catch (const std::runtime_error&)
  {
    if (attempts != 2)
      return 3;
  }

  attempts = 0;
  CctvStream cancelled("/nonexistent-cctv-recovery-test", [&]() -> std::string
  {
    ++attempts;
    running = 0;
    throw CctvTransientError("simulated timeout during shutdown");
  });
  if (cancelled.read(frame, running) || attempts != 1)
    return 4;
  std::cout << "PASS: transient refresh retry, fatal propagation, shutdown\n";
  return 0;
}

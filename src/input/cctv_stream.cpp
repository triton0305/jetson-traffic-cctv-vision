#include "input/cctv_stream.hpp"
#include "input/cctv_provider.hpp"

#include <chrono>
#include <iostream>
#include <thread>
#include <utility>

namespace
{
bool delay(int seconds, volatile std::sig_atomic_t& running)
{
  for (int i = 0; i < seconds * 10 && running; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  return running != 0;
}
}

CctvStream::CctvStream(std::string stream_url, std::function<std::string()> refresh_url)
  : stream_url_(std::move(stream_url)), refresh_url_(std::move(refresh_url))
{
}

bool CctvStream::open()
{
  release();
  if (!capture_.open(stream_url_, cv::CAP_FFMPEG,
                     {cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 10000,
                      cv::CAP_PROP_READ_TIMEOUT_MSEC, 10000}))
  {
    std::cerr << "CCTV Stream: FFMPEG open failed; check network, URL validity and codec/backend\n";
    return false;
  }
  std::cout << "CCTV Stream: backend=" << capture_.getBackendName() << '\n';
  return true;
}

bool CctvStream::readFrame(cv::Mat& frame)
{
  frame.release();
  if (!capture_.isOpened() || !capture_.read(frame) || frame.empty())
    return false;
  if (frame.size() != last_frame_size_)
  {
    last_frame_size_ = frame.size();
    std::cout << "CCTV Frame: " << frame.cols << 'x' << frame.rows
              << " bbox coordinates=original frame\n";
  }
  return true;
}

bool CctvStream::read(cv::Mat& frame, volatile std::sig_atomic_t& running)
{
  if (!running)
    return false;
  if (readFrame(frame))
    return true;
  std::cerr << "CCTV Stream: read failed; reconnecting cached URL\n";
  for (int attempt = 1; attempt <= 2 && running; ++attempt)
  {
    if (!delay(attempt, running))
      return false;
    if (open() && readFrame(frame))
    {
      std::cout << "CCTV Stream: cached URL reconnect succeeded\n";
      return true;
    }
  }
  // Only refresh after cached URL recovery fails. Wait also satisfies provider cooldown.
  // The provider caps refresh requests; persistent failure becomes a runtime error.
  while (running && refresh_url_)
  {
    std::cerr << "CCTV Stream: cached URL recovery failed; refresh after 30 seconds\n";
    if (!delay(30, running))
      return false;
    try
    {
      stream_url_ = refresh_url_();
    }
    catch (const CctvTransientError&)
    {
      if (!running)
        return false;
      std::cerr << "CCTV Stream: temporary refresh failure; retry cached URL, "
                   "then retry refresh after cooldown\n";
      if (open() && readFrame(frame))
      {
        std::cout << "CCTV Stream: cached URL recovered after refresh failure\n";
        return true;
      }
      continue;
    }
    if (open() && readFrame(frame))
    {
      std::cout << "CCTV Stream: refreshed URL reconnect succeeded\n";
      return true;
    }
  }
  return false;
}

void CctvStream::release()
{
  capture_.release();
}

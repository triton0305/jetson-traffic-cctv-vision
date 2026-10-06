#ifndef CCTV_STREAM_HPP
#define CCTV_STREAM_HPP

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <csignal>
#include <functional>
#include <string>

class CctvStream
{
public:
  CctvStream(std::string stream_url, std::function<std::string()> refresh_url);
  bool open();
  bool read(cv::Mat& frame, volatile std::sig_atomic_t& running);
  void release();

private:
  bool readFrame(cv::Mat& frame);
  std::string stream_url_;
  std::function<std::string()> refresh_url_;
  cv::VideoCapture capture_;
  cv::Size last_frame_size_;
};

#endif // CCTV_STREAM_HPP

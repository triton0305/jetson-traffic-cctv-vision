#ifndef UTIC_CCTV_PROVIDER_HPP
#define UTIC_CCTV_PROVIDER_HPP

#include "input/cctv_provider.hpp"

#include <chrono>
#include <cstddef>
#include <string>

std::string extractUticHlsUrl(const std::string& page);

struct UticCctvConfig
{
  std::string api_key;
  std::string cctv_id = "L010009";

  static UticCctvConfig fromEnvironment();
};

class UticCctvProvider : public CctvProvider
{
public:
  explicit UticCctvProvider(UticCctvConfig config);
  CctvEndpoint select(bool refresh = false) override;
  std::size_t calls() const { return calls_; }

private:
  UticCctvConfig config_;
  std::size_t calls_ = 0;
  std::chrono::steady_clock::time_point last_call_{};
};

#endif // UTIC_CCTV_PROVIDER_HPP

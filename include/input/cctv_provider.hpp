#ifndef CCTV_PROVIDER_HPP
#define CCTV_PROVIDER_HPP

#include <string>
#include <stdexcept>
#include <vector>

struct CctvEndpoint
{
  std::string name;
  double longitude;
  double latitude;
  std::string stream_url;
};

class CctvTransientError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class CctvProvider
{
public:
  virtual ~CctvProvider() = default;
  virtual CctvEndpoint select(bool refresh = false) = 0;
};

#endif // CCTV_PROVIDER_HPP

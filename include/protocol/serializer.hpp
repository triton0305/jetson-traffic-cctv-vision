#ifndef SERIALIZER_HPP
#define SERIALIZER_HPP

#include <string>
#include <vector>
#include "core/detection_result.hpp"

class Serializer
{
public:
  std::string serialize(
    const DetectionResult& result,
    const Detection& detection,
    const std::string& message_id) const;
  std::string serializeVehicleCount(
    const DetectionResult& result,
    std::size_t count,
    const std::string& message_id) const;
};

#endif // SERIALIZER_HPP

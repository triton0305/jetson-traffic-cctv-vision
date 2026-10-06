#include "protocol/serializer.hpp"
#include <nlohmann/json.hpp>
#include "core/config.hpp"

namespace
{
nlohmann::json envelope(const DetectionResult& result, const char* type,
                        const std::string& message_id)
{
  return {{"version", Config::PROTOCOL_VERSION}, {"type", type},
          {"device_id", Config::DEVICE_ID}, {"message_id", message_id},
          {"data", {{"frame_id", result.frame_id}, {"timestamp_ms", result.timestamp_ms}}}};
}
}

std::string Serializer::serialize(
  const DetectionResult& result,
  const Detection& detection,
  const std::string& message_id) const
{
  auto json = envelope(result, "vision", message_id);
  auto& data = json["data"];
  data["class_id"] = detection.class_id;
  data["class_name"] = detection.class_name;
  data["confidence"] = detection.confidence;
  data["bbox"] = {{"x", detection.bbox.x}, {"y", detection.bbox.y},
                  {"width", detection.bbox.width}, {"height", detection.bbox.height}};
  return json.dump();
}

std::string Serializer::serializeVehicleCount(
  const DetectionResult& result,
  std::size_t count,
  const std::string& message_id) const
{
  auto json = envelope(result, "vehicle_count", message_id);
  json["data"]["vehicle_count"] = count;
  return json.dump();
}

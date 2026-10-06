#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstddef>
#include <cstdint>

namespace Config
{
constexpr const char* DEVICE_ID = "vision-pi-01";
constexpr const char* BOOT_ID_PATH = BOOT_ID_FILE_PATH;

constexpr int PROTOCOL_VERSION = 1;
constexpr int RECONNECT_DELAY_MS = 1000;
constexpr std::size_t MAX_QUEUE_SIZE = 256;

}

#endif // CONFIG_HPP

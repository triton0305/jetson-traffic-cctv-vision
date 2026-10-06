#include "input/utic_cctv_provider.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace
{

bool rejectsEnvironment(const char* expected)
{
  try
  {
    UticCctvConfig::fromEnvironment();
  }
  catch (const std::runtime_error& error)
  {
    return std::string(error.what()).find(expected) != std::string::npos;
  }

  return false;
}

}

int main(int argc, char* argv[])
{
  if (argc == 2 && std::string(argv[1]) == "--inherited")
  {
    const auto config = UticCctvConfig::fromEnvironment();
    return config.api_key == "test-key" &&
      config.cctv_id == "L010009" ? 0 : 1;
  }

  const std::string page = R"(
    <div>It's the city's CCTV page.</div>
    <!-- <script>src: "https://example.invalid/html.m3u8";</script> -->
    <script>
    // src: "https://example.invalid/sample.m3u8";
    /* src: "https://example.invalid/old.m3u8"; */
    const config = {src: "https://example.invalid/live.m3u8?token=test&amp;x=1"};
    </script>
  )";
  if (extractUticHlsUrl(page) != "https://example.invalid/live.m3u8?token=test&x=1")
    return 7;
  try
  {
    extractUticHlsUrl("// https://example.invalid/comment.m3u8");
    return 8;
  }
  catch (const std::runtime_error&)
  {
  }

  if (extractUticHlsUrl(R"(<!-- <source src="https://example.invalid/old.m3u8"> -->
    <video><source src="https://example.invalid/current.m3u8"></video>)") !=
      "https://example.invalid/current.m3u8")
    return 9;

  const std::string extensionless = R"(
    // video_url = "https://example.invalid/old";
    /* video_url = "https://example.invalid/unused"; */
    video_url = "https://example.invalid/99/encrypted+token=";
    hls.loadSource(video_url);
  )";
  if (extractUticHlsUrl(extensionless) != "https://example.invalid/99/encrypted+token=")
    return 10;

  unsetenv("UTIC_API_KEY");
  if (!rejectsEnvironment("not present"))
    return 1;

  setenv("UTIC_API_KEY", "", 1);
  if (!rejectsEnvironment("empty"))
    return 2;

  setenv("UTIC_API_KEY", "test-key", 1);
  unsetenv("UTIC_CCTV_ID");
  const auto config = UticCctvConfig::fromEnvironment();

  if (config.api_key != "test-key" || config.cctv_id != "L010009")
    return 3;

  UticCctvProvider provider(config);

  setenv("UTIC_CCTV_ID", "L010012", 1);
  if (UticCctvConfig::fromEnvironment().cctv_id != "L010012")
    return 4;

  setenv("UTIC_CCTV_ID", "L010009", 1);
  const pid_t child = fork();
  if (child == -1)
    return 5;

  if (child == 0)
  {
    execl(argv[0], argv[0], "--inherited", static_cast<char*>(nullptr));
    _exit(127);
  }

  int status = 0;
  if (waitpid(child, &status, 0) != child ||
      !WIFEXITED(status) || WEXITSTATUS(status) != 0)
    return 6;

  std::cout << "PASS: UTIC configuration and process environment inheritance\n";
  return 0;
}

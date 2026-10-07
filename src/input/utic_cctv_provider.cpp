#include "input/utic_cctv_provider.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <regex>
#include <stdexcept>
#include <utility>

namespace
{

std::string env(const char* name, const std::string& fallback)
{
  const char* value = std::getenv(name);
  return value ? value : fallback;
}

struct CurlRuntime
{
  CurlRuntime()
  {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
      throw std::runtime_error("UTIC: curl initialization failed");
  }

  ~CurlRuntime()
  {
    curl_global_cleanup();
  }
};

std::size_t receive(char* data, std::size_t size, std::size_t count, void* context)
{
  auto& body = *static_cast<std::string*>(context);
  const std::size_t bytes = size * count;

  if (bytes > 16 * 1024 * 1024 ||
      body.size() > 16 * 1024 * 1024 - bytes)
    return 0;

  try
  {
    body.append(data, bytes);
    return bytes;
  }
  catch (...)
  {
    return 0;
  }
}

std::string escape(CURL* curl, const std::string& value)
{
  std::unique_ptr<char, decltype(&curl_free)> escaped(
    curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size())),
    curl_free);

  if (!escaped)
    throw std::runtime_error("UTIC: URL encoding failed");

  return escaped.get();
}

std::string valueOrUndefined(
  const nlohmann::json& object,
  const char* key)
{
  if (!object.contains(key) || object[key].is_null())
    return "undefined";

  if (object[key].is_string())
    return object[key].get<std::string>();

  if (object[key].is_number_integer())
    return std::to_string(object[key].get<long long>());

  return "undefined";
}

}


std::string extractUticHlsUrl(const std::string& html)
{
  const std::string uncommented = std::regex_replace(
    html, std::regex(R"(<!--[\s\S]*?-->)"), " ");
  const std::string& page = uncommented;

  const std::regex tokens(
    R"token(("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|`(?:\\.|[^`\\])*`)|/\*[\s\S]*?\*/|//[^\n]*)token");
  std::string active;
  std::size_t position = 0;
  for (auto it = std::sregex_iterator(page.begin(), page.end(), tokens);
       it != std::sregex_iterator(); ++it)
  {
    active += page.substr(position, it->position() - position);
    active += (*it)[1].matched ? (*it)[1].str() : " ";
    position = it->position() + it->length();
  }
  active += page.substr(position);
  const std::regex hls(R"(https?://[^"'<>[:space:]\\]+\.m3u8(?:\?[^"'<>[:space:]\\]*)?)");
  std::smatch match;
  std::string url;
  if (!std::regex_search(active, match, hls))
  {
    // Some UTIC players use encrypted, extensionless HLS URLs.
    const std::regex video_url(
      R"(\bvideo_url\s*=\s*["'](https?://[^"'<>[:space:]\\]+)["'])");
    if (!std::regex_search(active, match, video_url))
      throw std::runtime_error("UTIC: HLS stream URL not found");
    url = match[1].str();
  }
  else
    url = match.str();
  std::size_t pos = 0;
  while ((pos = url.find("&amp;", pos)) != std::string::npos)
  {
    url.replace(pos, 5, "&");
    ++pos;
  }
  return url;
}

UticCctvConfig UticCctvConfig::fromEnvironment()
{
  const char* api_key = std::getenv("UTIC_API_KEY");

  if (!api_key)
    throw std::runtime_error(
      "UTIC_API_KEY is not present in the process environment; "
      "export it in the same shell that launches edge_vision");

  if (!*api_key)
    throw std::runtime_error(
      "UTIC_API_KEY is empty in the process environment");

  UticCctvConfig config;
  config.api_key = api_key;
  config.cctv_id = env("UTIC_CCTV_ID", config.cctv_id);
  return config;
}

UticCctvProvider::UticCctvProvider(UticCctvConfig config)
  : config_(std::move(config))
{
  if (config_.api_key.empty())
    throw std::runtime_error(
      "UTIC_API_KEY is required (environment variable)");

  if (config_.cctv_id.empty())
    throw std::runtime_error(
      "UTIC_CCTV_ID must not be empty");
}

CctvEndpoint UticCctvProvider::select(bool refresh)
{
  if ((calls_ == 0 && refresh) ||
      (selected_ && !refresh))
    throw std::runtime_error(
      "UTIC: initial query allowed once; later queries require stream failure");

  if (calls_ >= 4)
    throw std::runtime_error(
      "UTIC: per-run refresh limit reached");

  const auto now = std::chrono::steady_clock::now();

  if (calls_ &&
      now - last_call_ < std::chrono::seconds(30))
    throw std::runtime_error(
      "UTIC: refresh cooldown has not elapsed");

  static CurlRuntime runtime;

  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(
    curl_easy_init(),
    curl_easy_cleanup);

  if (!curl)
    throw std::runtime_error(
      "UTIC: curl handle creation failed");

  curl_easy_setopt(curl.get(), CURLOPT_COOKIEFILE, "");
  curl_easy_setopt(
    curl.get(),
    CURLOPT_PROTOCOLS,
    CURLPROTO_HTTP | CURLPROTO_HTTPS);
  curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 20L);
  curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, receive);
  curl_easy_setopt(
    curl.get(),
    CURLOPT_USERAGENT,
    "jetson-traffic-cctv-vision/1.0");

  auto request =
    [&](const std::string& url,
        const std::string& referer,
        bool ajax,
        const char* stage)
  {
    std::string body;
    curl_slist* headers = nullptr;

    if (ajax)
    {
      headers = curl_slist_append(
        headers,
        "X-Requested-With: XMLHttpRequest");
      headers = curl_slist_append(
        headers,
        "Accept: application/json, text/javascript, */*; q=0.01");
    }

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(
      curl.get(),
      CURLOPT_REFERER,
      referer.empty() ? nullptr : referer.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);

    const auto status = curl_easy_perform(curl.get());

    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, nullptr);
    curl_slist_free_all(headers);

    if (status == CURLE_OPERATION_TIMEDOUT || status == CURLE_COULDNT_CONNECT ||
        status == CURLE_COULDNT_RESOLVE_HOST || status == CURLE_RECV_ERROR ||
        status == CURLE_SEND_ERROR || status == CURLE_PARTIAL_FILE)
      throw CctvTransientError(
        std::string("UTIC: ") + stage + " request failed: " + curl_easy_strerror(status));

    if (status != CURLE_OK)
      throw std::runtime_error(
        std::string("UTIC request failed: ") +
        curl_easy_strerror(status));

    long http = 0;
    curl_easy_getinfo(
      curl.get(),
      CURLINFO_RESPONSE_CODE,
      &http);

    if (http == 408 || http == 429 || (http >= 500 && http <= 599))
      throw CctvTransientError("UTIC: temporary HTTP request failure");

    if (http != 200)
      throw std::runtime_error(
        "UTIC HTTP status " +
        std::to_string(http));

    return body;
  };

  ++calls_;
  last_call_ = now;

  std::cout
    << "UTIC API: calls=" << calls_
    << " reason="
    << (refresh ? "stream_failure" : "startup")
    << '\n';

  const std::string open_url =
    "http://www.utic.go.kr/guide/cctvOpenData.do?key=" +
    escape(curl.get(), config_.api_key);

  const std::string list_body =
    request(open_url, "", false, "open-data");

  if (list_body.find(config_.cctv_id) ==
      std::string::npos)
  {
    const auto response = nlohmann::json::parse(list_body, nullptr, false);
    const auto* result = &response;

    if (response.is_array() && !response.empty())
      result = &response.front();

    if (result->is_object() && result->contains("resultCode"))
    {
      const auto& code = result->at("resultCode");
      // Only emit a short numeric status, never provider text or URLs.
      const std::string status = code.is_string() ? code.get<std::string>() : "";
      const bool safe_status = !status.empty() && status.size() <= 3 &&
        status.find_first_not_of("0123456789") == std::string::npos;

      throw std::runtime_error(
        "UTIC: open-data API returned a status response" +
        (safe_status ? " (resultCode=" + status + ")" : "") +
        "; check API key and registered source IP range");
    }

    throw std::runtime_error(
      "UTIC: requested CCTV ID absent from open-data response; "
      "the response may be an access/error page rather than a CCTV list");
  }

  const std::string info_url =
    "http://www.utic.go.kr/map/getCctvInfoById.do?cctvId=" +
    escape(curl.get(), config_.cctv_id);

  const std::string info_body =
    request(info_url, open_url, true, "metadata");

  const auto info =
    nlohmann::json::parse(
      info_body,
      nullptr,
      false);

  if (!info.is_object() ||
      !info.contains("CCTVID") ||
      !info.contains("CCTVNAME") ||
      !info.contains("ID"))
    throw std::runtime_error(
      "UTIC: CCTV metadata response mismatch");

  const std::string cctv_id =
    info.at("CCTVID").get<std::string>();

  const std::string cctv_name =
    info.at("CCTVNAME").get<std::string>();

  const double longitude =
    info.value("XCOORD", 0.0);

  const double latitude =
    info.value("YCOORD", 0.0);

  std::string kind =
    valueOrUndefined(info, "KIND");

  if (cctv_id.rfind("L01", 0) == 0)
    kind = "Seoul";

  const std::string ch =
    valueOrUndefined(info, "CH");

  const std::string id =
    valueOrUndefined(info, "ID");

  const std::string ip =
    valueOrUndefined(info, "CCTVIP");

  const std::string passwd =
    valueOrUndefined(info, "PASSWD");

  const std::string port =
    valueOrUndefined(info, "PORT");

  std::string stream_page =
    "http://www.utic.go.kr/jsp/map/openDataCctvStream.jsp?";

  auto parameter =
    [&](const char* name,
        const std::string& value)
  {
    if (stream_page.back() != '?')
      stream_page += '&';

    stream_page += name;
    stream_page += '=';
    stream_page += escape(curl.get(), value);
  };

  parameter("key", config_.api_key);
  parameter("cctvid", cctv_id);
  parameter("cctvName", cctv_name);
  parameter("kind", kind);
  parameter("cctvip", ip);
  parameter("cctvch", ch);
  parameter("id", id);
  parameter("cctvpasswd", passwd);
  parameter("cctvport", port);

  std::cout
    << "Selected CCTV: "
    << cctv_name
    << " id="
    << cctv_id
    << " longitude="
    << longitude
    << " latitude="
    << latitude
    << '\n';

  const std::string stream_body =
    request(stream_page, open_url, false, "playback-page");

  const std::string stream_url = extractUticHlsUrl(stream_body);

  selected_ = true;
  return CctvEndpoint{
    cctv_name,
    longitude,
    latitude,
    stream_url};
}

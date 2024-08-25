#pragma once

#include "http/Socket.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <oneapi/tbb/concurrent_hash_map.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
namespace ACPAcoro {

class httpHeaders {
public:
  httpHeaders()  = default;
  ~httpHeaders() = default;

  static inline std::unordered_set<std::string_view> const validRequestHeaders =
      {
          "Accept",
          "Accept-Encoding",
          "Connection",
          "Host",
          "User-Agent",
          "Content-Encoding",
          "Content-Type",
          "Content-Length",
  };

  static bool checkHeader(std::string_view header) {
    return validRequestHeaders.contains(header);
  };
  std::map<std::string, std::string> data;
};

class chunkedBody {
public:
  chunkedBody()  = default;
  ~chunkedBody() = default;
  std::vector<std::pair<size_t, std::string>> chunks;
};

class httpMessage {
public:
  httpMessage()          = default;
  virtual ~httpMessage() = default;

  enum class statusCode {
    OK                         = 200,
    BAD_REQUEST                = 400,
    NOT_FOUND                  = 404,
    LENTH_REQUIRED             = 411,
    INTERNAL_SERVER_ERROR      = 500,
    NOT_IMPLEMENTED            = 501,
    HTTP_VERSION_NOT_SUPPORTED = 505,
  };

  static inline std::unordered_map<statusCode,
                                   std::string_view> const statusStrings = {
      {                        statusCode::OK,                         "OK"},
      {               statusCode::BAD_REQUEST,                "Bad Request"},
      {                 statusCode::NOT_FOUND,                  "Not Found"},
      {            statusCode::LENTH_REQUIRED,            "Length Required"},
      {     statusCode::INTERNAL_SERVER_ERROR,      "Internal Server Error"},
      {           statusCode::NOT_IMPLEMENTED,            "Not Implemented"},
      {statusCode::HTTP_VERSION_NOT_SUPPORTED, "HTTP Version Not Supported"},
  };

  std::string version;
  httpHeaders headers;
  statusCode status;
};

class httpRequest : public httpMessage {
public:
  httpRequest()  = default;
  ~httpRequest() = default;

  httpRequest &parseResquest(reactorSocket &);
  void parseHeaders(std::string_view, httpHeaders &);

  static void checkMethod(std::string_view method);
  static void checkVersion(std::string_view version);

  enum class method {
    GET,
    HEAD,
  };

  static inline std::map<std::string_view, method> const methodStrings = {
      { "GET",  method::GET},
      {"HEAD", method::HEAD},
  };

  method method;
  std::filesystem::path uri;
  std::optional<std::variant<std::string, chunkedBody>> body;
  bool completed = false;

  static tbb::concurrent_hash_map<int, std::shared_ptr<std::string>>
      uncompletedRequests;
};

class httpResponse : public httpMessage {
public:
  httpResponse()  = default;
  ~httpResponse() = default;

  std::filesystem::path uri;
};

class invalidRequest : public std::runtime_error {
public:
  invalidRequest(
      httpMessage::statusCode status = httpMessage::statusCode::BAD_REQUEST)
      : std::runtime_error("Invalid request"), status(status) {}

  httpMessage::statusCode status;
};

class uncompletedRequest : public std::runtime_error {
public:
  uncompletedRequest() : std::runtime_error("Uncompleted request") {}
};

} // namespace ACPAcoro
#pragma once

#include "http/Socket.hpp"
#include "tl/expected.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <oneapi/tbb/concurrent_hash_map.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
namespace ACPAcoro {

enum class httpErrc {
  OK                         = 200,
  BAD_REQUEST                = 400,
  NOT_FOUND                  = 404,
  LENTH_REQUIRED             = 411,
  INTERNAL_SERVER_ERROR      = 500,
  NOT_IMPLEMENTED            = 501,
  HTTP_VERSION_NOT_SUPPORTED = 505,
  UNCOMPLETED_REQUEST        = 600,
};

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
  std::map<std::string, std::string> data{};
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

  using statusCode       = httpErrc;

  enum class method {
    GET,
    HEAD,
  };

  static inline std::map<std::string_view, method> const methodStrings = {
      { "GET",  method::GET},
      {"HEAD", method::HEAD},
  };

  method method       = method::HEAD;

  std::string version = "HTTP/1.1";
  httpHeaders headers{};
  statusCode status{httpErrc::OK};
};

class httpRequest : public httpMessage {
public:
  httpRequest()  = default;
  ~httpRequest() = default;

  tl::expected<void, std::error_code>
      parseResquest(std::shared_ptr<std::string>);
  tl::expected<void, std::error_code> parseHeaders(std::string_view &);
  tl::expected<void, std::error_code> parseFirstLine(std::string_view &);

  /**   @brief Read the request message from the socket
   *    @return A shared_ptr to the string that contains the request message
   *    @retval
   *    1) socketError::eofError: when the read encounter a eof
   *
   *    2) httpError::uncompletedRequest: when the read encounter a EWOULDBLOCK
   *       or EAGAIN and the request is not completed (end with "\r\n\r\n")
   *
   *    3) other errors: when the read encounter other error from the read
   *       operation except EWOULDBLOCK or EAGAIN
   */
  tl::expected<std::shared_ptr<std::string>, std::error_code>
  readRequest(reactorSocket &);

  static bool checkMethod(std::string_view method);
  static bool checkVersion(std::string_view version);
  static std::shared_ptr<std::string> getBuffer(int fd);
  static void eraseBuffer(int fd);

  httpRequest(httpRequest &&)                 = default;
  httpRequest &operator=(httpRequest &&)      = default;
  httpRequest(httpRequest const &)            = delete;
  httpRequest &operator=(httpRequest const &) = delete;

  std::filesystem::path uri{};
  bool completed = false;

  static tbb::concurrent_hash_map<int, std::shared_ptr<std::string>>
      uncompletedRequests;
};

class httpResponse : public httpMessage {
public:
  httpResponse()  = default;
  ~httpResponse() = default;

  // inline static std::unordered_set<std::string_view> const MIMEtypes{
  //     "text/html",       "text/plain",
  //     "image/jpeg",      "image/png",
  //     "image/gif",       "application/json",
  //     "application/xml", "application/octet-stream"};
  //
  static inline std::unordered_map<std::string_view, std::string_view> const
      extensionMap{
          {   "html",                "text/html"},
          {    "txt",               "text/plain"},
          {   "jpeg",               "image/jpeg"},
          {    "jpg",               "image/jpeg"},
          {    "png",                "image/png"},
          {    "gif",                "image/gif"},
          {   "json",         "application/json"},
          {    "xml",          "application/xml"},
          {    "bin", "application/octet-stream"},
          {    "css",                 "text/css"},
          {     "js",   "application/javascript"},
          {   "webp",               "image/webp"},
          {    "ico",             "image/x-icon"},
          {    "svg",            "image/svg+xml"},
          {    "pdf",          "application/pdf"},
          {    "zip",          "application/zip"},
          {   "woff",    "application/font-woff"},
          {  "woff2",   "application/font-woff2"},
          {"pf_meta", "application/octet-stream"},
  };

  static std::shared_ptr<std::string> const notFoundResponse;
  static std::shared_ptr<std::string> const badRequestResponse;
  static std::shared_ptr<std::string> const notImplementedResponse;

  /**   @brief Make a response message from the request
   *    @return shared_ptr to the response message
   *    @param request: the request message
   *   @retval
   *   1) shared_ptr: success
   */
  httpResponse(httpRequest &, std::filesystem::path const);

  /** @brief Serialize the response message to a string
   * @return shared_ptr to the string that contains the response message
   * */
  std::shared_ptr<std::string> serialize();

  std::filesystem::path uri{};
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

inline auto const &httpErrorCode() {
  static struct httpErrorCategory : public std::error_category {
    char const *name() const noexcept override { return "httpError"; }

    std::string message(int c) const override {
      switch (static_cast<httpErrc>(c)) {
      case httpErrc::OK:
        return "OK";
      case httpErrc::BAD_REQUEST:
        return "Bad Request";
      case httpErrc::NOT_FOUND:
        return "Not Found";
      case httpErrc::LENTH_REQUIRED:
        return "Length Required";
      case httpErrc::INTERNAL_SERVER_ERROR:
        return "Internal Server Error";
      case httpErrc::NOT_IMPLEMENTED:
        return "Not Implemented";
      case httpErrc::HTTP_VERSION_NOT_SUPPORTED:
        return "HTTP Version Not Supported";
      case httpErrc::UNCOMPLETED_REQUEST:
        return "Uncompleted Request";
      default:
        return "Unknown error";
      }
    }
  } instance;
  return instance;
}

inline std::error_code make_error_code(httpErrc e) {
  return {static_cast<int>(e), httpErrorCode()};
}

inline std::error_condition make_error_condition(httpErrc e) {
  return {static_cast<int>(e), httpErrorCode()};
}

} // namespace ACPAcoro

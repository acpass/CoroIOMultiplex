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

class httpHeaders {
public:
  httpHeaders() = default;
  ~httpHeaders() = default;

  static inline std::unordered_set<std::string_view> const validRequestHeaders =
      {
          "Accept",     "Accept-Encoding",  "Connection",   "Host",
          "User-Agent", "Content-Encoding", "Content-Type", "Content-Length",
  };

  static bool checkHeader(std::string_view header) {
    return validRequestHeaders.contains(header);
  };
  std::map<std::string, std::string> data;
};

class chunkedBody {
public:
  chunkedBody() = default;
  ~chunkedBody() = default;
  std::vector<std::pair<size_t, std::string>> chunks;
};

class httpMessage {
public:
  httpMessage() = default;
  virtual ~httpMessage() = default;

  enum class statusCode {
    OK = 200,
    BAD_REQUEST = 400,
    NOT_FOUND = 404,
    LENTH_REQUIRED = 411,
    INTERNAL_SERVER_ERROR = 500,
    NOT_IMPLEMENTED = 501,
    HTTP_VERSION_NOT_SUPPORTED = 505,
  };

  static inline std::unordered_map<statusCode,
                                   std::string_view> const statusStrings = {
      {statusCode::OK, "OK"},
      {statusCode::BAD_REQUEST, "Bad Request"},
      {statusCode::NOT_FOUND, "Not Found"},
      {statusCode::LENTH_REQUIRED, "Length Required"},
      {statusCode::INTERNAL_SERVER_ERROR, "Internal Server Error"},
      {statusCode::NOT_IMPLEMENTED, "Not Implemented"},
      {statusCode::HTTP_VERSION_NOT_SUPPORTED, "HTTP Version Not Supported"},
  };

  std::string version;
  httpHeaders headers;
  statusCode status;
};

class httpRequest : public httpMessage {
public:
  httpRequest() = default;
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

  enum class method {
    GET,
    HEAD,
  };

  static inline std::map<std::string_view, method> const methodStrings = {
      {"GET", method::GET},
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
  httpResponse() = default;
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

enum class httpErrc {
  success = 0,
  badRequest,
  uncompletedRequest,
};

inline auto const &httpErrorCode() {
  static struct httpErrorCategory : public std::error_category {
    char const *name() const noexcept override { return "httpError"; }

    std::string message(int c) const override {
      switch (static_cast<httpErrc>(c)) {
      case httpErrc::success:
        return "Success";
      case httpErrc::badRequest:
        return "Bad request";
      case httpErrc::uncompletedRequest:
        return "Uncompleted request";
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

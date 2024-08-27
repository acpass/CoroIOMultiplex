#include "http/Http.hpp"

#include "http/Socket.hpp"
#include "tl/expected.hpp"

#include <exception>
#include <expected>
#include <memory>
#include <oneapi/tbb/concurrent_hash_map.h>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace ACPAcoro {

tbb::concurrent_hash_map<int, std::shared_ptr<std::string>>
    httpRequest::uncompletedRequests;

bool httpRequest::checkMethod(std::string_view method) {
  return methodStrings.contains(method);
}

bool httpRequest::checkVersion(std::string_view version) {
  return version != "HTTP/1.1";
}

std::shared_ptr<std::string> httpRequest::getBuffer(int fd) {
  decltype(uncompletedRequests)::accessor uncompletedRequestsAccessor;
  if (uncompletedRequests.find(uncompletedRequestsAccessor, fd)) {
    return uncompletedRequestsAccessor->second;

  } else {
    auto buffer = std::make_shared<std::string>();
    uncompletedRequests.emplace(fd, buffer);
    return buffer;
  }
}

void httpRequest::eraseBuffer(int fd) {
  uncompletedRequests.erase(fd);
}

// read the request message from the socket
tl::expected<std::shared_ptr<std::string>, std::error_code>
httpRequest::readRequest(reactorSocket &socket) {
  std::println("Reading request message for socket {}", socket.fd);
  char buffer[1024];
  auto requestMessage = getBuffer(socket.fd);
  while (!this->completed) {
    auto readResult =
        socket
            .read(buffer, 1024)

            // check if enconter the eof
            .and_then([&](int bytesRead) -> tl::expected<int, std::error_code> {
              if (bytesRead == 0) {
                return tl::unexpected(make_error_code(socketError::eofError));
              } else if (bytesRead + requestMessage->size() > 4096) {
                return tl::unexpected(make_error_code(httpErrc::badRequest));
              }
              return bytesRead;
            })

            // append the data to the requestMessage
            .map([&](int bytesRead) {
              requestMessage->append(buffer, bytesRead);
            })

            // handle the error
            // if the error is EWOULDBLOCK or EAGAIN,
            // check if the request is completed
            // if so, erase the buffer and return
            // otherwise, return an error
            .or_else([&](auto const &e) -> tl::expected<void, std::error_code> {
              if (e == make_error_code(
                           std::errc::resource_unavailable_try_again) ||
                  e == make_error_code(std::errc::operation_would_block)) {
                std::println("EWOULDBLOCK or EAGAIN");

                if (requestMessage->ends_with("\r\n\r\n")) {
                  eraseBuffer(socket.fd);
                  this->completed = true;
                  return {};
                }

                std::print("request now:\n{}", *requestMessage);

                return tl::unexpected(
                    make_error_code(httpErrc::uncompletedRequest));
              }

              // error case 3
              eraseBuffer(socket.fd);
              return tl::unexpected(e);
            });

    if (!readResult)
      return tl::unexpected(readResult.error());
  }

  return requestMessage;
}

// TODO: parse throw a badRequest error when the request is OK
tl::expected<void, std::error_code>
httpRequest::parseResquest(std::shared_ptr<std::string> requestMsg) {

  // std::println("Parsing request message for socket {}", socket.fd);

  std::string_view request(*requestMsg);

  return this->parseFirstLine(request)
      .and_then([&]() -> tl::expected<void, std::error_code> {
        // parse the headers
        return parseHeaders(request);
      })
      .and_then([&]() -> tl::expected<void, std::error_code> {
        if (this->headers.data.contains("Connection") &&
            this->headers.data["Connection"] == "close") {
          return tl::unexpected(make_error_code(socketError::eofError));
        }
        return {};
      });
}

tl::expected<void, std::error_code>
httpRequest::parseHeaders(std::string_view &request) {
  // fields are delimited by CRLF
  size_t pos = request.find("\r\n");
  while (pos != std::string_view::npos && pos != 0) {

    std::string_view header = request.substr(0, pos);
    request.remove_prefix(pos + 2);
    pos = header.find(':');

    if (pos == std::string_view::npos) {
      return tl::unexpected(make_error_code(httpErrc::badRequest));
    }

    std::string key(header.substr(0, pos));
    if (httpHeaders::checkHeader(key)) {
      header.remove_prefix(pos + 1);
      while (header[0] == ' ' && !header.empty()) {
        header.remove_prefix(1);
      }
      if (!header.empty() && !(header[0] == '\r')) {
        this->headers.data[std::move(key)] = header;
      } else {
        return tl::unexpected(make_error_code(httpErrc::badRequest));
      }
      pos = request.find("\r\n");
    }
  }
  return {};
}

tl::expected<void, std::error_code>
httpRequest::parseFirstLine(std::string_view &request) {
  this->status = httpMessage::statusCode::OK;
  // find the end of the request line
  size_t pos = request.find("\r\n");
  if (pos == std::string_view::npos) {
    return tl::unexpected(make_error_code(httpErrc::badRequest));
  }
  std::string_view requestLine = request.substr(0, pos);
  // std::println("Request line: {}", requestLine);

  // remove the request line from the view
  request.remove_prefix(pos + 2);

  // parse the request line
  pos = requestLine.find(' ');
  if (pos == std::string_view::npos) {
    return tl::unexpected(make_error_code(httpErrc::badRequest));
  }
  std::string_view method = requestLine.substr(0, pos);
  // std::println("Method: {}", method);

  if (!checkMethod(method)) {
    return tl::unexpected(make_error_code(httpErrc::badRequest));
  }

  requestLine.remove_prefix(pos + 1);
  // parse the uri
  pos = requestLine.find(' ');
  if (pos == std::string_view::npos) {
    return tl::unexpected(make_error_code(httpErrc::badRequest));
  }
  std::string_view uri = requestLine.substr(0, pos);

  requestLine.remove_prefix(pos + 1);

  // parse the version
  std::string_view version = requestLine;
  checkVersion(version);

  this->method  = methodStrings.at(method);
  this->uri     = uri;
  this->version = version;
  return {};
}

} // namespace ACPAcoro
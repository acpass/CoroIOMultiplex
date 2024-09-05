#include "http/Http.hpp"

#include "http/Socket.hpp"
#include "tl/expected.hpp"

#include <chrono>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <oneapi/tbb/concurrent_hash_map.h>
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
  return version == "HTTP/1.1";
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

void httpRequest::eraseBuffer(int fd) { uncompletedRequests.erase(fd); }

// read the request message from the socket
// when it read a uncompletedRequests
// the process will be saved in a buffer
tl::expected<std::shared_ptr<std::string>, std::error_code>
httpRequest::readRequest(reactorSocket &socket) {
  // std::println("Reading request message for socket {}", socket.fd);
  char buffer[1024];
  auto requestMessage = getBuffer(socket.fd);

  // read loop
  // until the read return a error_code
  // case 1)  EAGAIN
  // case 2)  EOF
  // case 3)  others
  while (true) {

    auto readResult =
        socket
            .read(buffer, 1024)

            // check if enconter the eof
            .and_then([&](int bytesRead) -> tl::expected<int, std::error_code> {
              if (bytesRead == 0) {
                return tl::unexpected(make_error_code(socketError::eofError));

              } else if (bytesRead + requestMessage->size() > 4096) {
                // if the request is too long
                return tl::unexpected(make_error_code(httpErrc::BAD_REQUEST));
              }

              return bytesRead;
            })

            // append the data to the requestMessage
            .map([&](int bytesRead) {
              requestMessage->append(buffer, bytesRead);
            });

    // handle the error
    // if the error is EWOULDBLOCK or EAGAIN,
    // check if the request is completed
    // if so, erase the buffer and return
    // otherwise, return an error
    if (!readResult) {
      // in this three cases, check if the request is completed
      if (readResult.error() ==
              make_error_code(std::errc::resource_unavailable_try_again) ||
          readResult.error() ==
              make_error_code(std::errc::operation_would_block)) {
        // std::println("EWOULDBLOCK or EAGAIN");

        // if so, return as Success
        if (requestMessage->ends_with("\r\n\r\n")) {
          eraseBuffer(socket.fd);
          return requestMessage;
        }
        // otherwise, the requestMessage is uncompletedRequests
        // return error
        return tl::unexpected(make_error_code(httpErrc::UNCOMPLETED_REQUEST));

      } else {
        eraseBuffer(socket.fd);
        return tl::unexpected(readResult.error());
      }
    }
  }
}

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
  // std::println("Parsing headers:");
  // std::print("Request: \n{}", request);
  // fields are delimited by CRLF
  size_t pos = request.find("\r\n");

  while (pos != 0) {

    if (pos == std::string_view::npos) {
      status = httpErrc::BAD_REQUEST;
      return tl::unexpected(make_error_code(httpErrc::BAD_REQUEST));
    }
    // get header line
    std::string_view headerLine = request.substr(0, pos);
    request.remove_prefix(pos + 2);

    // parse the header line
    // get key
    pos = headerLine.find(':');
    // check if the header line is valid
    // unvalid case:
    // 1) no colon in the header line
    // 2) colon is the first character
    if (pos == std::string_view::npos || pos == 0) {
      status = httpErrc::BAD_REQUEST;
      return tl::unexpected(make_error_code(httpErrc::BAD_REQUEST));
    }
    std::string_view key = headerLine.substr(0, pos);

    // split the key and value
    headerLine.remove_prefix(pos + 1);
    // get value
    if (headerLine.empty()) {
      status = httpErrc::BAD_REQUEST;
      return tl::unexpected(make_error_code(httpErrc::BAD_REQUEST));
    }
    std::string_view value = headerLine;
    headers.data.emplace(key, value);

    pos = request.find("\r\n");
  }
  return {};
}

tl::expected<void, std::error_code>
httpRequest::parseFirstLine(std::string_view &request) {

  this->status = httpMessage::statusCode::OK;
  // find the end of the request line
  size_t pos = request.find("\r\n");
  if (pos == std::string_view::npos) {
    // std::println("failed to parse in first phase");
    status = httpErrc::BAD_REQUEST;
    return tl::unexpected(make_error_code(httpErrc::BAD_REQUEST));
  }
  std::string_view requestLine = request.substr(0, pos);
  // std::println("Request line: {}", requestLine);

  // remove the request line from the view
  request.remove_prefix(pos + 2);

  // parse the request line
  pos = requestLine.find(' ');
  if (pos == std::string_view::npos) {
    // std::println("failed to parse in second phase");
    status = httpErrc::BAD_REQUEST;
    return tl::unexpected(make_error_code(httpErrc::BAD_REQUEST));
  }
  std::string_view method = requestLine.substr(0, pos);
  // std::println("Method: {}", method);

  if (!checkMethod(method)) {
    // std::println("failed to parse in third phase");
    status = httpErrc::BAD_REQUEST;
    return tl::unexpected(make_error_code(httpErrc::BAD_REQUEST));
  }

  requestLine.remove_prefix(pos + 1);
  // parse the uri
  pos = requestLine.find(' ');
  if (pos == std::string_view::npos) {
    // std::println("failed to parse in fourth phase");
    status = httpErrc::BAD_REQUEST;
    return tl::unexpected(make_error_code(httpErrc::BAD_REQUEST));
  }
  std::string_view uri = requestLine.substr(0, pos);

  requestLine.remove_prefix(pos + 1);

  // parse the version
  std::string_view version = requestLine;

  // std::println("Version: {}", version);

  if (!checkVersion(version)) {
    // std::println("failed to parse in fifth phase");
    status = httpErrc::BAD_REQUEST;
    return tl::unexpected(make_error_code(httpErrc::BAD_REQUEST));
  }

  this->method = methodStrings.at(method);
  this->uri = std::move(uri);
  this->version = std::move(version);
  // std::println("Success to parse the firstline");
  return {};
}

std::shared_ptr<std::string> const httpResponse::notFoundResponse =
    std::make_shared<std::string>("HTTP/1.1 404 Not Found\r\n"
                                  "Content-Length: 0\r\n"
                                  "\r\n");

std::shared_ptr<std::string> const httpResponse::badRequestResponse =
    std::make_shared<std::string>("HTTP/1.1 400 Bad Request\r\n"
                                  "Content-Length: 0\r\n"
                                  "\r\n");

std::shared_ptr<std::string> const httpResponse::notImplementedResponse =
    std::make_shared<std::string>("HTTP/1.1 501 Not Implemented\r\n"
                                  "Content-Length: 0\r\n"
                                  "\r\n");

/** TODO
 * 1) if the status is not OK, return the status
 */
httpResponse::httpResponse(httpRequest &request,
                           std::filesystem::path const webRoot) {

  // std::println("webRoot: {}", webRoot.string());

  if (request.status != httpMessage::statusCode::OK) {
    status = request.status;
    return;
  }

  // if (!checkPath(request.uri)) {
  //   return tl::unexpected(make_error_code(httpErrc::NOT_FOUND));
  // }

  version = request.version;
  status = request.status;
  method = request.method;
  uri = std::filesystem::weakly_canonical(request.uri);
  auto &requestHeaders = request.headers.data;

  if (uri.string().ends_with("/")) {
    uri /= "index.html";
  }

  // println("Request uri: {}", response->uri.string());

  if (!uri.has_extension()) {
    status = httpMessage::statusCode::NOT_FOUND;
    return;
  }

  auto argsPos = uri.string().find('?');
  if (argsPos != std::string::npos) {
    uri = uri.string().substr(0, argsPos);
  }

  std::string extension = uri.extension().string().substr(1);

  std::string_view MIMEfulltype;
  // std::println("Extension: {}", extension);
  if (!extensionMap.contains(extension)) {
    MIMEfulltype = "application/octet-stream";
  } else {
    MIMEfulltype = extensionMap.at(extension);
  }
  std::string MIMEWildcardType{MIMEfulltype.substr(0, MIMEfulltype.find('/'))};
  MIMEWildcardType.append("/*");

  // check if the client is able to accept this type of file
  if (!requestHeaders.contains("Accept")) {
    status = httpMessage::statusCode::BAD_REQUEST;
    return;
  }

  std::string_view const acceptedMIMEtypes = requestHeaders.at("Accept");
  if (!acceptedMIMEtypes.find("*/*") &&
      !acceptedMIMEtypes.find(MIMEWildcardType) &&
      !acceptedMIMEtypes.find(MIMEfulltype)) {
    status = httpMessage::statusCode::NOT_FOUND;
    return;
  }

  std::error_code ec{};
  auto realPath = std::filesystem::canonical(webRoot / uri.relative_path(), ec);
  // println("[{}]: Real path: {}", std::chrono::utc_clock::now(),
  // realPath.string());
  if (ec.value() != 0) {
    status = httpMessage::statusCode::NOT_FOUND;
    return;
  }

  auto Content_Length = std::filesystem::file_size(realPath, ec);

  if (ec.value() != 0) {
    status = httpMessage::statusCode::INTERNAL_SERVER_ERROR;
    return;
  }

  uri = realPath;

  headers.data.emplace("Content-Length", std::to_string(Content_Length));
  headers.data.emplace("Content-Type", MIMEfulltype);
  headers.data.emplace("Catch-Control", "no-cache");

  return;
}

std::shared_ptr<std::string> httpResponse::serialize() {
  switch (status) {
  case httpMessage::statusCode::NOT_FOUND:
    return notFoundResponse;
  case httpMessage::statusCode::NOT_IMPLEMENTED:
    return notImplementedResponse;
  case httpMessage::statusCode::INTERNAL_SERVER_ERROR:
  case httpMessage::statusCode::HTTP_VERSION_NOT_SUPPORTED:
  case httpMessage::statusCode::LENTH_REQUIRED:
  case httpMessage::statusCode::BAD_REQUEST:
    return badRequestResponse;

  default:
    auto response = std::make_shared<std::string>();
    response->append(std::format("{} {} {}\r\n", version, (int)status,
                                 httpErrorCode().message((int)status)));
    for (auto &headerLine : headers.data) {
      response->append(
          std::format("{}: {}\r\n", headerLine.first, headerLine.second));
    }
    response->append("\r\n");
    return response;
  }
}

} // namespace ACPAcoro

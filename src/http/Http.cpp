#include "http/Http.hpp"

#include "http/Socket.hpp"

#include <exception>
#include <memory>
#include <oneapi/tbb/concurrent_hash_map.h>
#include <print>
#include <string>
#include <string_view>
#include <system_error>

namespace ACPAcoro {

tbb::concurrent_hash_map<int, std::shared_ptr<std::string>>
    httpRequest::uncompletedRequests;

void httpRequest::checkMethod(std::string_view method) {
  if (!methodStrings.contains(method)) {
    throw invalidRequest(httpMessage::statusCode::NOT_IMPLEMENTED);
  }
}

void httpRequest::checkVersion(std::string_view version) {
  if (version != "HTTP/1.1") {
    throw invalidRequest(httpMessage::statusCode::HTTP_VERSION_NOT_SUPPORTED);
  }
}

httpRequest &httpRequest::parseResquest(reactorSocket &socket) {
  char buffer[1024];

  try {
    std::println("Parsing request from socket {}", socket.fd);
    std::shared_ptr<std::string> requestMessage;

    // check if there is an uncompleted request
    // if so, append the new data to the requestMessage
    // otherwise, create a new requestMessage and insert it into the map
    decltype(uncompletedRequests)::accessor uncompletedRequestsAccessor;
    if (!uncompletedRequests.find(uncompletedRequestsAccessor, socket.fd)) {
      std::println("Creating new request message for socket {}", socket.fd);
      requestMessage = std::make_shared<std::string>();
      uncompletedRequests.emplace(socket.fd, requestMessage);
    } else {
      std::println("Appending to uncompleted request message for socket {}",
                   socket.fd);
      requestMessage = uncompletedRequestsAccessor->second;
      uncompletedRequestsAccessor.release();
    }

    // read all data from the socket for edge-triggered epoll
    while (!this->completed) {
      try {

        size_t read = socket.recv(buffer, 1024);
        if (read == 0) {
          std::println("EOF on socket {}", socket.fd);
          uncompletedRequests.erase(socket.fd);
          throw eofException();
        }
        requestMessage->append(buffer, read);

      } catch (std::error_code &e) {

        // no more data to read
        if (e.value() == EAGAIN || e.value() == EWOULDBLOCK) {
          std::println("Request now:\n{}", *requestMessage);

          // uncompleted Requests
          // throw out to wait for the next epoll
          if (!requestMessage->ends_with("\r\n\r\n")) {

            std::println("Uncompleted request on socket {}", socket.fd);

            throw uncompletedRequest();

            // completed Requests
          } else {
            std::println("Completed request on socket {}", socket.fd);

            uncompletedRequests.erase(socket.fd);
            std::println("Erased uncompleted request for socket {}", socket.fd);
            this->completed = true;
          }
        } else {
          uncompletedRequests.erase(socket.fd);
          std::rethrow_exception(std::current_exception());
        }
      } catch (...) {
        uncompletedRequests.erase(socket.fd);
        std::rethrow_exception(std::current_exception());
      }

      std::println("while loop end side");
    } // end while
    std::println("while loop end");

    // now the full request is in the requestMessage
    std::println("Parsing request message for socket {}", socket.fd);

    this->status = httpMessage::statusCode::OK;
    // find the end of the request line
    std::string_view request(*requestMessage);

    size_t pos = request.find("\r\n");
    if (pos == std::string_view::npos) {
      throw invalidRequest();
    }
    std::string_view requestLine = request.substr(0, pos);
    std::println("Request line: {}", requestLine);

    // remove the request line from the view
    request.remove_prefix(pos + 2);

    // parse the request line
    pos = requestLine.find(' ');
    if (pos == std::string_view::npos) {
      throw invalidRequest();
    }
    std::string_view method = requestLine.substr(0, pos);
    std::println("Method: {}", method);
    checkMethod(method);

    requestLine.remove_prefix(pos + 1);
    // parse the uri
    pos = requestLine.find(' ');
    if (pos == std::string_view::npos) {
      throw invalidRequest();
    }
    std::string_view uri = requestLine.substr(0, pos);

    requestLine.remove_prefix(pos + 1);

    // parse the version
    std::string_view version = requestLine;
    checkVersion(version);

    this->method  = methodStrings.at(method);
    this->uri     = uri;
    this->version = version;

    this->parseHeaders(request, this->headers);

    if (this->headers.data.contains("Connection") &&
        this->headers.data["Connection"] == "close") {
      throw eofException();
    }
  } catch (invalidRequest &e) {
    status = e.status;
  } catch (...) {
    std::rethrow_exception(std::current_exception());
  }
  return *this;
}

void httpRequest::parseHeaders(std::string_view request, httpHeaders &headers) {
  // fields are delimited by CRLF
  size_t pos = request.find("\r\n");
  while (pos != std::string_view::npos && pos != 0) {

    std::string_view header = request.substr(0, pos);
    request.remove_prefix(pos + 2);
    pos = header.find(':');
    if (pos == std::string_view::npos) {
      throw invalidRequest();
    }

    std::string key(header.substr(0, pos));
    if (httpHeaders::checkHeader(key)) {
      header.remove_prefix(pos + 1);
      headers.data[key] = header;
    }
    pos = request.find("\r\n");
  }
}

} // namespace ACPAcoro
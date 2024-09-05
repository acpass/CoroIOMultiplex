
#include "http/Http.hpp"

#include "file/File.hpp"
#include "tl/expected.hpp"

#include <async/Epoll.hpp>
#include <async/Loop.hpp>
#include <async/Tasks.hpp>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <http/Socket.hpp>
#include <memory>
#include <print>
#include <string>
#include <sys/epoll.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace ACPAcoro;

auto &threadPoolInst = threadPool::getInstance();
auto epollInst = epollInstance(threadPoolInst);

std::filesystem::path webroot{"/var/www/"};

// char const *dummyResponse{"HTTP/1.1 200 OK\r\n"
//                           "Content-Length: 13\r\n"
//                           "Content-Type: text/html\r\n"
//                           "Cache-Control: no-cache\r\n\r\n"
//                           "hello world\r\n"};
// size_t const dummyResponseSize = strlen(dummyResponse);
//
// char const *badRequestResponse{"HTTP/1.1 400 Bad Request\r\n"
//                                "Content-Length: 0\r\n"};
// size_t const badRequestResponseSize = strlen(badRequestResponse);
//
Task<> responseHandler(std::shared_ptr<reactorSocket> socket,
                       httpRequest request) {

  httpResponse response(request, webroot);
  auto responseStr = response.serialize();

  int on = 1;
  int off = 0;

  setsockopt(socket->fd, SOL_TCP, TCP_CORK, &on, sizeof(on));

  while (true) {
    size_t totalsend = 0;
    size_t restSize = responseStr->size();
    auto sendResult = socket->send(responseStr->data() + totalsend, restSize);

    if (!sendResult) {
      if (sendResult.error() ==
              std::make_error_code(std::errc::resource_unavailable_try_again) ||
          sendResult.error() ==
              make_error_code(std::errc::no_message_available)) {
        co_await threadPoolInst.scheduler;
      } else {
        std::println("Error: {}", sendResult.error().message());
        co_return;
      }

    } // error handle

    else {
      totalsend += sendResult.value();
      restSize -= sendResult.value();
      if (totalsend >= responseStr->size()) {
        break;
      }
    } // send success
  }

  if (response.method == ACPAcoro::httpMessage::method::HEAD ||
      response.status != httpResponse::statusCode::OK) {
    co_return;
  }

  regularFile file;
  while (true) {
    auto openResult = file.open(response.uri);

    if (!openResult) {
      if (openResult.error() ==
              make_error_code(std::errc::too_many_files_open) ||
          openResult.error() ==
              make_error_code(std::errc::too_many_files_open_in_system)) {
        co_await threadPoolInst.scheduler;
      } else {
        println("Error: {}", openResult.error().message());
        co_return;
      }
    } else {
      // if open successfully, break the loop
      break;
    }
  }

  // TODO: fix bug
  // few seconds after launch,
  // the sendfile will block
  while (true) {
    long sendBytes = 0;
    // off_t offset = 0;
    long size = file.size;
    auto sendResult = socket->sendfile(file.fd, nullptr, size);

    if (!sendResult) {
      if (sendResult.error() ==
              std::make_error_code(std::errc::resource_unavailable_try_again) ||
          sendResult.error() ==
              make_error_code(std::errc::no_message_available)) {
        co_await threadPoolInst.scheduler;
      } else {
        std::println("Error: {}", sendResult.error().message());
        co_return;
      }

    } // error handle

    else {
      sendBytes += sendResult.value();
      size -= sendResult.value();
      if (sendBytes >= (long)file.size || size <= 0) {
        break;
      }
    } // send success
  }

  setsockopt(socket->fd, SOL_TCP, TCP_CORK, &off, sizeof(off));
  // response to the client
  // httpResponse response{};
  // std::println("Handling response from socket {}", socket->fd);
  // if (status == httpResponse::statusCode::OK) {
  //
  //   socket->send(dummyResponse, dummyResponseSize)
  //       .map_error([&](auto const &e) {
  //         std::println("Error: {}", e.message());
  //         return e;
  //       });
  // }
  // // response a bad request or internal server error
  // else {
  //   socket->send(badRequestResponse, badRequestResponseSize)
  //       .map_error([&](auto const &e) {
  //         std::println("Error: {}", e.message());
  //         return e;
  //       });
  // }
  //
  // if (status == httpResponse::statusCode::OK) {
  //   // std::println("Handling request from socket {}", socket->fd);
  //   std::println("Request: {}", request->uri.string());
  //   std::println("Status: {}", httpErrorCode().message((int)status));
  //   std::println("Headers: ");
  //   for (auto &[key, value] : request->headers.data) {
  //     std::println("  {}: {}", key, value);
  //   }
  // } else {
  //   std::println("Handling error from socket {}", socket->fd);
  //   std::println("Status: {}", httpErrorCode().message((int)status));
  // }
  co_return;
}

Task<> httpHandle(std::shared_ptr<reactorSocket> socket) {
  // std::println("Handling socket {}", socket->fd);

  httpRequest request;

  auto requestResult =
      request
          // read request
          .readRequest(*socket)
          // parse request
          .and_then([&](auto strPtr) -> tl::expected<void, std::error_code> {
            return request.parseResquest(strPtr);
          })
          // add response task
          .and_then([&]() -> tl::expected<void, std::error_code> {
            // std::println("Adding response task");
            request.status = httpResponse::statusCode::OK;
            auto responseTask = responseHandler(socket, std::move(request));
            loopInstance::getInstance().addTask(responseTask.detach(), true);
            return {};
          })
          // error handle phase
          // if the request is uncompleted, ignore it
          // otherwise, return the error
          .or_else([&](auto const &e) -> tl::expected<void, std::error_code> {
            if (e == make_error_code(httpErrc::BAD_REQUEST)) {
              // std::println("Bad request");
              request.status = httpResponse::statusCode::BAD_REQUEST;
              auto responseTask = responseHandler(socket, std::move(request));
              loopInstance::getInstance().addTask(responseTask.detach(), true);
              return {};
            } else if (e != make_error_code(socketError::eofError) &&
                       e != make_error_code(httpErrc::UNCOMPLETED_REQUEST) &&
                       e != make_error_code(std::errc::connection_reset)) {
              // std::println("Internal server error");
              // std::println("Error: {}", e.message());
              auto responseTask = responseHandler(socket, std::move(request));
              loopInstance::getInstance().addTask(responseTask.detach(), true);
              return {};
            }

            return tl::unexpected(e);
          });

  if (!requestResult) {
    if (requestResult.error() ==
        make_error_code(httpErrc::UNCOMPLETED_REQUEST)) {
      // mean yield
      co_await threadPoolInst.scheduler;
    }
    co_return;
  } else {
    co_await threadPoolInst.scheduler;
  }
}

Task<> co_main(std::string const &port) {
  auto server = std::make_unique<serverSocket>(port);

  server->listen();
  auto fd = server->fd;

  epoll_event event;
  event.events = EPOLLIN;
  event.data.ptr =
      acceptAll(std::move(server), httpHandle, epollInst, threadPoolInst)
          .detach()
          .address();
  epollInst.addEvent(fd, &event);

  threadPoolInst.addTask(epollInst.epollWaitEvent(100).detach());
  threadPoolInst.enter();

  co_return;
}

int main(int argc, char *argv[]) {
  std::string port;

  if (argc < 2) {
    port = "12312";
  } else {
    port = argv[1];
  }

  if (argc >= 3) {
    webroot = argv[2];
  }

  auto mainTask = co_main(port);
  while (!mainTask.selfCoro.done()) {
    mainTask.resume();
  }

  return 0;
}

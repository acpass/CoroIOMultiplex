
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
#include <memory_resource>
#include <print>
#include <string>
#include <sys/epoll.h>
#include <system_error>
#include <thread>
#include <vector>

using namespace ACPAcoro;
std::pmr::synchronized_pool_resource poolResource{};

const std::filesystem::path webroot{"/home/acpass/www/"};

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
Task<int, yieldPromiseType<int>> responseHandler(
    std::shared_ptr<reactorSocket> socket, std::shared_ptr<httpRequest> request,
    httpResponse::statusCode status = httpResponse::statusCode::OK) {

  request->status = status;
  auto response = httpResponse::makeResponse(*request, webroot);
  auto responseStr = response->serialize();
  socket->send(responseStr->data(), responseStr->size())
      .and_then(
          [&](int)
              -> tl::expected<std::shared_ptr<regularFile>, std::error_code> {
            return regularFile::open(response->uri);
          })
      .and_then([&](auto file) -> tl::expected<int, std::error_code> {
        return socket->sendfile(file->fd, NULL, file->size);
      })
      .map_error(
          [&](auto const &e) { std::println("Error: {}", e.message()); });

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
  co_return 0;
}

tl::expected<void, std::error_code>
httpHandle(std::shared_ptr<reactorSocket> socket) {
  // std::println("Handling socket {}", socket->fd);

  while (true) {
    auto request = std::allocate_shared<httpRequest>(
        std::pmr::polymorphic_allocator<httpRequest>(&poolResource));

    auto requestResult =
        request
            // read request
            ->readRequest(*socket)
            // parse request
            .and_then([&](auto strPtr) -> tl::expected<void, std::error_code> {
              return request->parseResquest(strPtr);
            })
            // add response task
            .and_then([&]() -> tl::expected<void, std::error_code> {
              // std::println("Adding response task");
              auto responseTask = responseHandler(socket, request);
              loopInstance::getInstance().addTask(responseTask.detach());
              return {};
            })
            // error handle phase
            // if the request is uncompleted, ignore it
            // otherwise, return the error
            .or_else([&](auto const &e) -> tl::expected<void, std::error_code> {
              if (e == make_error_code(httpErrc::BAD_REQUEST)) {
                // std::println("Bad request");
                auto responseTask = responseHandler(
                    socket, nullptr, httpResponse::statusCode::BAD_REQUEST);
                loopInstance::getInstance().addTask(responseTask.detach());
              } else if (e != make_error_code(socketError::eofError) &&
                         e != make_error_code(httpErrc::UNCOMPLETED_REQUEST)) {
                // std::println("Internal server error");
                // std::println("Error: {}", e.message());
                auto responseTask = responseHandler(
                    socket, nullptr,
                    httpResponse::statusCode::INTERNAL_SERVER_ERROR);
                loopInstance::getInstance().addTask(responseTask.detach());
              }

              return tl::unexpected(e);
            });

    if (!requestResult) {
      if (requestResult.error() ==
          make_error_code(httpErrc::UNCOMPLETED_REQUEST)) {
        return {};
      }
      return tl::unexpected(requestResult.error());
    }

    // if (!readResult) {
    //   if (readResult.error() ==
    //   make_error_code(httpErrc::uncompletedRequest)) {
    //     return std::nullopt;
    //   } else {
    //     // eof and other errors
    //     return readResult.error();
    //   }
    // }

    // request
    //     ->parseResquest(*readResult.value())

    //     // and_then mean there is an error
    //     .and_then([&](auto const &e) -> std::optional<std::error_code> {
    //       if (e == make_error_code(httpErrc::badRequest)) {
    //         auto responseTask = responseHandler(
    //             socket, nullptr, httpResponse::statusCode::BAD_REQUEST);
    //         loopInstance::getInstance().addTask(responseTask.detach());
    //       } else {
    //         auto responseTask = responseHandler(
    //             socket,
    //             nullptr,
    //             httpResponse::statusCode::INTERNAL_SERVER_ERROR);
    //         loopInstance::getInstance().addTask(responseTask.detach());
    //       }

    //       return e;
    //     })
    //     // or_else mean there is no error
    //     // bussiness logic
    //     .or_else([&]() -> std::optional<std::error_code> {
    //       auto responseTask = responseHandler(socket, request);
    //       loopInstance::getInstance().addTask(responseTask.detach());
    //       return std::nullopt;
    //     });
    // // } catch (eofException &e) {
    //   std::rethrow_exception(std::current_exception());
    // } catch (uncompletedRequest &e) {
    //   return std::nullopt;
    // } catch (invalidRequest &e) {
    //   auto responseTask = responseHandler(socket, nullptr, e.status);
    //   loopInstance::getInstance().addTask(responseTask.detach());
    // } catch (...) {
    //   auto responseTask = responseHandler(
    //       socket, nullptr,
    //       httpResponse::statusCode::INTERNAL_SERVER_ERROR);
    //   loopInstance::getInstance().addTask(responseTask.detach());
    // }
  }
}

void runTasks() {
  auto &loop = loopInstance::getInstance();
  while (true) {
    loop.runTasks();
  }
}

Task<> co_main(std::string const &port) {
  auto server = std::make_shared<serverSocket>(port);

  server->listen();

  auto &loop = loopInstance::getInstance();
  auto &epoll = epollInstance::getInstance();

  epoll_event event;
  event.events = EPOLLIN | EPOLLET;
  event.data.ptr = acceptAll(*server, httpHandle).detach().address();
  epoll.addEvent(server->fd, &event);

  loop.addTask(epollWaitEvent().detach(), true);

  std::vector<std::jthread> threads;
  for (int _ = 0; _ < 3; _++) {
    threads.emplace_back(runTasks);
  }

  while (true) {
    loop.runTasks();
  }
}

int main(int argc, char *argv[]) {
  std::string port;

  if (argc < 2) {
    port = "12312";
  } else {
    port = argv[1];
  }
  auto mainTask = co_main(port);
  loopInstance::getInstance().addTask(mainTask);
  while (!mainTask.selfCoro.done()) {
    loopInstance::getInstance().runTasks();
  }

  return 0;
}

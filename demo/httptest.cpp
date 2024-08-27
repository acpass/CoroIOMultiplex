
#include "http/Http.hpp"

#include "tl/expected.hpp"

#include <async/Epoll.hpp>
#include <async/Loop.hpp>
#include <async/Tasks.hpp>
#include <cstdio>
#include <exception>
#include <http/Socket.hpp>
#include <memory>
#include <memory_resource>
#include <optional>
#include <print>
#include <string>
#include <sys/epoll.h>
#include <system_error>
#include <thread>
#include <vector>

using namespace ACPAcoro;
std::pmr::synchronized_pool_resource poolResource{};

Task<int, yieldPromiseType<int>> responseHandler(
    std::shared_ptr<reactorSocket> socket,
    std::shared_ptr<httpRequest> request,
    httpResponse::statusCode status = httpResponse::statusCode::OK) {

  if (status == httpResponse::statusCode::OK) {
    std::println("Handling request from socket {}", socket->fd);
    std::println("Request: {}", request->uri.string());
    std::println("Status: {}", httpResponse::statusStrings.at(status));
    std::println("Headers: ");
    for (auto &[key, value] : request->headers.data) {
      std::println("  {}: {}", key, value);
    }
  } else {
    std::println("Handling error from socket {}", socket->fd);
    std::println("Status: {}", httpResponse::statusStrings.at(status));
  }
  co_return 0;
}

tl::expected<void, std::error_code>
httpHandle(std::shared_ptr<reactorSocket> socket) {
  std::println("Handling socket {}", socket->fd);

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
              std::println("Adding response task");
              auto responseTask = responseHandler(socket, request);
              loopInstance::getInstance().addTask(responseTask.detach());
              return {};
            })
            // error handle phase
            // if the request is uncompleted, ignore it
            // otherwise, return the error
            .or_else([&](auto const &e) -> tl::expected<void, std::error_code> {
              if (e == make_error_code(httpErrc::badRequest)) {
                std::println("Bad request");
                auto responseTask = responseHandler(
                    socket, nullptr, httpResponse::statusCode::BAD_REQUEST);
                loopInstance::getInstance().addTask(responseTask.detach());
              } else if (e != make_error_code(socketError::eofError) &&
                         e != make_error_code(httpErrc::uncompletedRequest)) {
                std::println("Internal server error");
                std::println("Error: {}", e.message());
                auto responseTask = responseHandler(
                    socket,
                    nullptr,
                    httpResponse::statusCode::INTERNAL_SERVER_ERROR);
                loopInstance::getInstance().addTask(responseTask.detach());
              }

              return tl::unexpected(e);
            });

    if (!requestResult) {
      if (requestResult.error() ==
          make_error_code(httpErrc::uncompletedRequest)) {
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
    //       socket, nullptr, httpResponse::statusCode::INTERNAL_SERVER_ERROR);
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

  auto &loop  = loopInstance::getInstance();
  auto &epoll = epollInstance::getInstance();

  epoll_event event;
  event.events   = EPOLLIN | EPOLLET;
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
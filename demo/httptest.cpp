
#include "http/Http.hpp"

#include <async/Epoll.hpp>
#include <async/Loop.hpp>
#include <async/Tasks.hpp>
#include <cstdio>
#include <exception>
#include <http/Socket.hpp>
#include <memory>
#include <memory_resource>
#include <print>
#include <string>
#include <sys/epoll.h>
#include <thread>
#include <vector>

using namespace ACPAcoro;
std::pmr::synchronized_pool_resource poolResource{};

Task<int, yieldPromiseType<int>> responseHandler(
    std::shared_ptr<reactorSocket> socket,
    std::shared_ptr<httpRequest> request,
    httpResponse::statusCode status = httpResponse::statusCode::OK) {

  std::println("Handling request from socket {}", socket->fd);
  std::println("Request: {}", request->uri.string());
  std::println("Status: {}", httpResponse::statusStrings.at(status));
  std::println("Headers: ");
  for (auto &[key, value] : request->headers.data) {
    std::println("  {}: {}", key, value);
  }

  co_return 0;
}

void httpHandle(std::shared_ptr<reactorSocket> socket) {
  std::println("Handling socket {}", socket->fd);

  while (true) {
    try {
      auto request = std::allocate_shared<httpRequest>(
          std::pmr::polymorphic_allocator<httpRequest>(&poolResource));
      request->parseResquest(*socket);

      auto responseTask = responseHandler(socket, request);
      loopInstance::getInstance().addTask(responseTask.detach());
    } catch (eofException &e) {
      std::rethrow_exception(std::current_exception());
    } catch (uncompletedRequest &e) {
      return;
    } catch (invalidRequest &e) {
      auto responseTask = responseHandler(socket, nullptr, e.status);
      loopInstance::getInstance().addTask(responseTask.detach());
    } catch (...) {
      auto responseTask = responseHandler(
          socket, nullptr, httpResponse::statusCode::INTERNAL_SERVER_ERROR);
      loopInstance::getInstance().addTask(responseTask.detach());
    }
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
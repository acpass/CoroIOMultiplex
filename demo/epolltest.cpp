
#include "async/Epoll.hpp"

#include "async/Loop.hpp"
#include "async/Tasks.hpp"
#include "http/Socket.hpp"

#include <cerrno>
#include <cstdio>
#include <exception>
#include <print>
#include <sys/epoll.h>
#include <system_error>
#include <thread>
#include <vector>
using namespace ACPAcoro;

void echoHandle(reactorSocket &socket) {
  char buffer[1024];

  while (true) {
    try {
      auto read = socket.recv(buffer, sizeof(buffer));
      if (read == 0) {
        throw eofException();
      }
      socket.send(buffer, read);
      // std::print("Read: {}", std::string_view(buffer, read));

    } catch (std::error_code const &e) {
      if (e.value() == EAGAIN || e.value() == EWOULDBLOCK) {
        return;
      } else if (e.value() == ECONNRESET) {
        std::println("Connection reset");
        return;
      } else {
        std::println("Error: {}", e.message());
        std::rethrow_exception(std::current_exception());
      }
    } catch (eofException const &) {
      std::println("connection closed");
      std::rethrow_exception(std::current_exception());
    }
  }
}

void runTasks() {
  while (true) {
    loopInstance::getInstance().runAll();
  }
}

Task<> co_main() {
  auto &epoll = epollInstance::getInstance();
  auto server = serverSocket("12312");
  server.listen();
  std::println("Listening on port 12312");
  auto waitTask = epollWaitEvent();
  epoll_event event;
  event.events   = EPOLLIN | EPOLLONESHOT;
  event.data.ptr = acceptAll(server, echoHandle).detach().address();
  try {
    epoll.addEvent(server.fd, &event);
  } catch (std::error_code const &e) {
    std::println("Error: {}", e.message());
    std::terminate();
  }

  std::vector<std::jthread> threads;
  for (int _ = 0; _ < 4; _++) {
    threads.emplace_back(runTasks);
  }

  while (true) {
    waitTask.resume();
    loopInstance::getInstance().runHighPriority();
    event.events   = EPOLLIN | EPOLLONESHOT;
    event.data.ptr = acceptAll(server, echoHandle).detach().address();
    epoll.modifyEvent(server.fd, &event);
  }

  co_return;
}

int main() {
  auto mainTask = co_main();
  loopInstance::getInstance().addTask(mainTask);

  while (!mainTask.selfCoro.done()) {
    loopInstance::getInstance().runAll();
  }

  return 0;
}
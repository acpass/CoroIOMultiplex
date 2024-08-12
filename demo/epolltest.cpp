
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
        // std::println("Connection reset");
        return;
      } else {
        // std::println("Error: {}", e.message());
        std::rethrow_exception(std::current_exception());
      }
    } catch (eofException const &) {
      // std::println("connection closed");
      std::rethrow_exception(std::current_exception());
    }
  }
}

void runTasks() {
  std::println("Starting task loop on thread {}", std::this_thread::get_id());
  while (true) {
    loopInstance::getInstance().runTasks();
  }
}

Task<> co_main() {
  auto &epoll = epollInstance::getInstance();
  auto server = serverSocket("12312");
  server.listen();
  std::println("Listening on port 12312");
  auto waitTask = epollWaitEvent(-1, true);
  epoll_event event;
  event.events   = EPOLLIN;
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

  loopInstance::getInstance().addTask(waitTask.detach());

  while (true) {
    loopInstance::getInstance().runTasks();
  }

  co_return;
}

int main() {
  auto mainTask = co_main();
  loopInstance::getInstance().addTask(mainTask);
  std::println("Starting main task");

  while (!mainTask.selfCoro.done()) {
    loopInstance::getInstance().runTasks();
  }

  return 0;
}
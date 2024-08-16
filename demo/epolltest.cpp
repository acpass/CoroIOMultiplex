
#include "async/Epoll.hpp"

#include "async/Loop.hpp"
#include "async/Tasks.hpp"
#include "http/Socket.hpp"
#include "utils.hpp/BufferPool.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <memory>
#include <memory_resource>
#include <print>
#include <span>
#include <sys/epoll.h>
#include <system_error>
#include <thread>
#include <vector>
using namespace ACPAcoro;

std::pmr::synchronized_pool_resource poolResource{
    {.max_blocks_per_chunk = 1024, .largest_required_pool_block = 1024}
};
// bufferPool<char, 1024> bufferPoolInstance;

struct readBuffer {
  char buffer[1024];
};

void pooltest() {
  auto ptr = std::allocate_shared<int>(
      std::pmr::polymorphic_allocator<int>(&poolResource), 40);
}

Task<> writer(std::shared_ptr<reactorSocket> socket,
              // std::span<char> buffer,
              // char *buffer,
              std::shared_ptr<readBuffer> buffer,
              size_t size) {
  // std::println("Writing to socket {}", socket->fd);
  socket->send(buffer->buffer, size);
  // bufferPoolInstance.returnBuffer(buffer);
  // delete[] buffer;
  co_return;
}

void echoHandle(std::shared_ptr<reactorSocket> socket) {
  // char buffer[1024];

  while (true) {
    try {
      // auto buffer = bufferPoolInstance.getBuffer();
      auto buffer = std::allocate_shared<readBuffer>(
          std::pmr::polymorphic_allocator<readBuffer>(&poolResource));
      auto read = socket->recv(buffer->buffer, 1024);
      if (read == 0) {
        // delete[] buffer;
        // bufferPoolInstance.returnBuffer(buffer);
        throw eofException();
      }

      loopInstance::getInstance().addTask(
          writer(socket, buffer, read).detach());

      // socket->send(buffer, read);
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
  auto waitTask = epollWaitEvent(200, true);
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
  for (int _ = 0; _ < 3; _++) {
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
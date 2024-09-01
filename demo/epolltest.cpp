
#include "async/Epoll.hpp"

#include "async/Loop.hpp"
#include "async/Tasks.hpp"
#include "http/Socket.hpp"
#include "tl/expected.hpp"
#include "utils.hpp/BufferPool.hpp"
#include "utils.hpp/ErrorHandle.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <memory>
#include <memory_resource>
#include <optional>
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

Task<int, yieldPromiseType<int>> writer(std::shared_ptr<reactorSocket> socket,
                                        // std::span<char> buffer,
                                        // char *buffer,
                                        std::shared_ptr<readBuffer> buffer,
                                        size_t size) {
  // std::println("Writing to socket {}", socket->fd);
  while (true) {
    size_t written  = 0;
    auto sendResult = socket->send(buffer->buffer + written, size);
    if (!sendResult) {
      if (sendResult.error() ==
              make_error_code(std::errc::resource_unavailable_try_again) ||
          sendResult.error() ==
              make_error_code(std::errc::operation_would_block)) {
        co_yield {};
      } else {
        std::println("Error: {}", sendResult.error().message());
        co_return {};
      }
    } else {
      written += sendResult.value();
      size    -= sendResult.value();
      if (written >= size) {
        co_return {};
      }
    }
    // bufferPoolInstance.returnBuffer(buffer);
    // delete[] buffer;
  }
}
// bug: if the client finish sending, but do not close the connection
// the server will create the last writer task, and yield to wait for the
// epoll
tl::expected<void, std::error_code>
echoHandle(std::shared_ptr<reactorSocket> socket) {
  // char buffer[1024];

  while (true) {
    // auto buffer = bufferPoolInstance.getBuffer();
    auto buffer = std::allocate_shared<readBuffer>(
        std::pmr::polymorphic_allocator<readBuffer>(&poolResource));
    auto read =
        socket->recv(buffer->buffer, 1024)
            .and_then([&](int readCnt) -> tl::expected<int, std::error_code> {
              if (readCnt == 0) {
                return tl::unexpected(make_error_code(socketError::eofError));
              }
              return readCnt;
            })
            .and_then([&](int readCnt) -> tl::expected<int, std::error_code> {
              loopInstance::getInstance().addTask(
                  writer(socket, buffer, readCnt).detach(), true);

              return readCnt;
            });

    if (!read) {
      if (read.error() ==
              make_error_code(std::errc::resource_unavailable_try_again) ||
          read.error() == make_error_code(std::errc::operation_would_block)) {
        return {};
      }
      return tl::unexpected(read.error());
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
  auto server = std::make_shared<serverSocket>("12312");
  server->listen();
  std::println("Listening on port 12312");
  auto waitTask = epollWaitEvent();
  epoll_event event;
  event.events   = EPOLLIN;
  event.data.ptr = acceptAll(server, echoHandle).detach().address();
  try {
    epoll.addEvent(server->fd, &event);
  } catch (std::error_code const &e) {
    std::println("Error: {}", e.message());
    std::terminate();
  }

  std::vector<std::jthread> threads;
  for (int _ = 0; _ < 15; _++) {
    threads.emplace_back(runTasks);
  }

  loopInstance::getInstance().addTask(waitTask.detach(), true);

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

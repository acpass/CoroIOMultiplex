
#include "async/Epoll.hpp"

#include "async/Loop.hpp"
#include "async/Tasks.hpp"
#include "http/Socket.hpp"
#include "tl/expected.hpp"
#include "utils/DEBUG.hpp"

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

struct readBuffer {
  char buffer[1024];
};

auto &threadPoolInst = threadPool::getInstance();

auto epollInst = epollInstance(threadPoolInst);

Task<> writer(std::shared_ptr<reactorSocket> socket,
              // std::span<char> buffer,
              // char *buffer,
              std::shared_ptr<std::string> buffer, size_t size) {
  // std::println("Writing to socket {}", socket->fd);
  auto fullsize = buffer->size();
  while (true) {
    size_t written = 0;
    auto sendResult = socket->send(buffer->data() + written, size);
    if (!sendResult) {
      if (sendResult.error() ==
              make_error_code(std::errc::resource_unavailable_try_again) ||
          sendResult.error() ==
              make_error_code(std::errc::operation_would_block)) {
        co_await threadPoolInst.scheduler;
      } else {
        std::println("Error: {}", sendResult.error().message());
        co_return;
      }
    } else {
      written += sendResult.value();
      size -= sendResult.value();
      if (written >= fullsize) {
        co_return;
      }
    }
    // bufferPoolInstance.returnBuffer(buffer);
    // delete[] buffer;
  }
}
// bug: if the client finish sending, but do not close the connection
// the server will create the last writer task, and yield to wait for the
// epoll
//
Task<> echoHandle(std::shared_ptr<reactorSocket> socket) {
  char tmpBuffer[1024];

  debug("start handle fd: {}", socket->fd);
  while (true) {
    auto buffer = std::make_shared<std::string>();
    // any individual request
    while (true) {
      // auto buffer = bufferPoolInstance.getBuffer();

      auto read =
          socket->recv(tmpBuffer, 1024)
              .and_then([&](int readCnt) -> tl::expected<int, std::error_code> {
                if (readCnt == 0) {
                  return tl::unexpected(make_error_code(socketError::eofError));
                }
                return readCnt;
              })
              .and_then([&](int readCnt) -> tl::expected<int, std::error_code> {
                buffer->append(tmpBuffer, readCnt);
                return readCnt;
              });

      if (!read) {
        if (read.error() ==
                make_error_code(std::errc::resource_unavailable_try_again) ||
            read.error() == make_error_code(std::errc::operation_would_block) ||
            read.error() == make_error_code(socketError::eofError)) {

          threadPoolInst.addTask(
              writer(socket, buffer, buffer->size()).detach());

          if (read.error() != make_error_code(socketError::eofError)) {
            co_await threadPoolInst.scheduler;
            break;
          } else {
            co_return;
          }
        } else {
          co_return;
        }
      }
    }
  }
}

Task<> co_main() {

  debug("enter co_main");
  auto server = std::make_unique<serverSocket>("12312");
  server->listen();
  auto fd = server->fd;
  std::println("Listening on port 12312");

  epoll_event event;
  event.events = EPOLLIN;
  event.data.ptr =
      acceptAll(std::move(server), echoHandle, epollInst, threadPoolInst)
          .detach()
          .address();

  epollInst.addEvent(fd, &event);

  threadPoolInst.addTask(epollInst.epollWaitEvent().detach());
  threadPoolInst.enter();

  co_return;
}

int main() {
  auto mainTask = co_main();

  while (!mainTask.selfCoro.done()) {
    mainTask.resume();
  }
  return 0;
}

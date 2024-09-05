
#include "http/Http.hpp"

#include "file/File.hpp"
#include "tl/expected.hpp"
#include "utils/DEBUG.hpp"

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
#include <sys/mman.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace ACPAcoro;

auto &threadPoolInst = threadPool::getInstance();
auto epollInst = epollInstance(threadPoolInst);

std::filesystem::path webroot{"/var/www/"};

Task<> responseHandler(std::shared_ptr<reactorSocket> socket,
                       httpRequest request) {

  httpResponse response(request, webroot);
  auto responseStr = response.serialize();

  int on = 1;
  int off = 0;

  setsockopt(socket->fd, SOL_TCP, TCP_CORK, &on, sizeof(on));

  while (true) {
    std::string_view sendData = *responseStr;
    auto sendResult = socket->send(sendData.data(), sendData.size());

    if (!sendResult) {
      if (sendResult.error() ==
              std::make_error_code(std::errc::resource_unavailable_try_again) ||
          sendResult.error() ==
              make_error_code(std::errc::no_message_available)) {
        co_await threadPoolInst.scheduler;
        continue;
      } else {
        debug("Error: {}", sendResult.error().message());
        co_return;
      }

    } // error handle
    else {
      sendData.remove_prefix(sendResult.value());
      if (sendData.empty()) {
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
        debug("Error: {}", openResult.error().message());
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
    size_t sendBytes = 0;
    // off_t offset = 0;
    size_t restSize = file.size;
    auto fileMem =
        ::mmap(nullptr, file.size, PROT_READ, MAP_PRIVATE, file.fd, 0);

    auto sendResult = socket->send(((char *)fileMem) + sendBytes, restSize);

    if (!sendResult) {
      if (sendResult.error() ==
              std::make_error_code(std::errc::resource_unavailable_try_again) ||
          sendResult.error() ==
              make_error_code(std::errc::no_message_available)) {
        co_await threadPoolInst.scheduler;
        continue;
      } else {
        debug("Error: {}", sendResult.error().message());
        munmap(fileMem, file.size);
        co_return;
      }

    } // error handle
    else {
      sendBytes += sendResult.value();
      restSize -= sendResult.value();
      if (sendBytes >= file.size) {
        munmap(fileMem, file.size);
        break;
      }
    } // send success
  } // while end

  setsockopt(socket->fd, SOL_TCP, TCP_CORK, &off, sizeof(off));

  co_return;
}

Task<> httpHandle(std::shared_ptr<reactorSocket> socket) {
  // std::println("Handling socket {}", socket->fd);
  while (true) {
    httpRequest request;
    request.status = ACPAcoro::httpErrc::OK;
    std::shared_ptr<std::string> requestStr;

    while (true) {
      auto readResult = request.readRequest(*socket);

      if (!readResult) {
        if (readResult.error() == make_error_code(socketError::eofError) ||
            readResult.error() ==
                make_error_code(std::errc::connection_reset)) {
          co_return;
        } else if (readResult.error() ==
                   make_error_code(httpErrc::UNCOMPLETED_REQUEST)) {
          co_await threadPoolInst.scheduler;
          continue;

        } else {
          request.status = ACPAcoro::httpErrc::INTERNAL_SERVER_ERROR;
          break;
        }
      } else {
        request.status = ACPAcoro::httpErrc::OK;
        requestStr = readResult.value();
        break;
      }
    } // read loop

    if (request.status == ACPAcoro::httpErrc::OK) {
      request.parseResquest(requestStr);
    }

    bool closeSession =
        request.status == ACPAcoro::httpErrc::OK &&
        request.headers.data.contains("Connection") &&
        request.headers.data.at("Connection") == std::string_view("Close");

    threadPoolInst.addTask(
        responseHandler(socket, std::move(request)).detach());

    if (closeSession)
      co_return;

  } // socket loop
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

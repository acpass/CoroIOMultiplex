
#include "async/Loop.hpp"
#include "async/Tasks.hpp"
#include "async/Uring.hpp"
#include "file/File.hpp"
#include "http/Http.hpp"
#include "http/Socket.hpp"
#include "tl/expected.hpp"
#include "uring/Socket.hpp"
#include <filesystem>
#include <memory>
#include <print>
#include <string>
#include <sys/mman.h>
#include <system_error>

using namespace ACPAcoro;

template <typename T> using expectedRet = tl::expected<T, std::error_code>;

auto &threadPoolInst = threadPool::getInstance();
uringInstance uringInst{threadPoolInst};
std::filesystem::path webRoot;

Task<> responseHandler(std::shared_ptr<asyncSocket> client,
                       httpRequest request) {

  httpResponse response(request, webRoot);
  auto responseStr = response.serialize();

  int on = 1;
  int off = 0;

  setsockopt(client->fd, SOL_TCP, TCP_CORK, &on, sizeof(on));

  while (true) {
    std::string_view sendData = *responseStr;
    auto sendResult =
        co_await client->send(sendData.data(), sendData.size(), 0, uringInst);

    if (!sendResult) {
      if (sendResult.error() ==
              std::make_error_code(std::errc::resource_unavailable_try_again) ||
          sendResult.error() ==
              make_error_code(std::errc::no_message_available) ||
          sendResult.error() == make_error_code(uringErr::sqeBusy)) {

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

    auto sendResult = co_await client->send(((char *)fileMem) + sendBytes,
                                            restSize, 0, uringInst);

    if (!sendResult) {
      if (sendResult.error() ==
              std::make_error_code(std::errc::resource_unavailable_try_again) ||
          sendResult.error() ==
              make_error_code(std::errc::no_message_available) ||
          sendResult.error() == make_error_code(uringErr::sqeBusy)) {
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

  setsockopt(client->fd, SOL_TCP, TCP_CORK, &off, sizeof(off));

  co_return;
}

Task<expectedRet<std::unique_ptr<std::string>>>
readRequest(asyncSocket &client) {

  auto request = std::make_unique<std::string>();
  char buf[1024];

  while (true) {

    auto readRes = co_await client.recv(buf, sizeof(buf), 0, uringInst);

    if (!readRes) {
      if (readRes.error() == make_error_code(uringErr::sqeBusy)) {
        co_await threadPoolInst.scheduler;
        continue;
      }
      co_return tl::unexpected(readRes.error());
    }

    if (readRes.value() == 0) {
      co_return tl::unexpected(make_error_code(socketError::eofError));
    }

    if ((readRes.value() + request->size()) > 4096) {
      co_return tl::unexpected(make_error_code(httpErrc::BAD_REQUEST));
    }

    request->append(buf, readRes.value());

    if (request->ends_with("\r\n\r\n")) {
      break;
    }
  }

  co_return std::move(request);
}

Task<> clientHandle(int fd) {
  auto client = std::make_shared<asyncSocket>(fd);

  while (true) {
    httpRequest request;
    request.status = ACPAcoro::httpErrc::OK;

    auto requestMsg = co_await readRequest(*client);

    if (!requestMsg) {
      if (requestMsg.error().category() == httpErrorCode()) {
        request.status = (httpErrc)requestMsg.error().value();
      } else {
        co_return;
      }
    }

    if (request.status == ACPAcoro::httpErrc::OK) {
      request.parseResquest(std::move(requestMsg.value()));
    }

    bool closeSession =
        request.status == ACPAcoro::httpErrc::OK &&
        request.headers.data.contains("Connection") &&
        request.headers.data.at("Connection") == std::string_view("Close");

    threadPoolInst.addTask(
        responseHandler(client, std::move(request)).detach());

    if (closeSession)
      co_return;
  }
}

int main(int argc, char **argv) {
  if (argc < 3) {
    std::println("Usage: {} [port] [webRoot directory]", argv[0]);
    return 0;
  }

  std::string port = argv[1];
  webRoot = argv[2];

  auto server = std::make_unique<serverSocket>(port);
  server->listen();
  debug("Server launch");
  threadPoolInst.addTask(uringInst.reapIOs().detach());
  threadPoolInst.addTask(
      asyncAccept(std::move(server), clientHandle, uringInst).detach());
  threadPoolInst.enter();
}

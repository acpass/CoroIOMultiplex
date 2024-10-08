#pragma once

// TODO: use co_await style io operation
//   e.g. co_await read(socket, buffer);
//   read will issue a io_uring read operation related with the caller coroutine
//   when the operation is done, the coroutine will be add to scheduler
//   and resume when it's turn
//
//
#include "async/Uring.hpp"
#include "http/Socket.hpp"
#include "utils/DEBUG.hpp"
#include <coroutine>
#include <cstddef>
#include <functional>
#include <memory>
#include <system_error>

namespace ACPAcoro {

struct sendAwaiter {
  bool await_ready() { return false; }
  bool await_suspend(std::coroutine_handle<> coro) {
    callerData.handle = coro;
    callerData.multishot = false;
    auto addRes = uring.prep_send(fd, buf, len, flags, &callerData);
    if (!addRes) {
      callerData.returnVal = tl::unexpected(addRes.error());
      return false;
    }
    return true;
  }

  tl::expected<int, std::error_code> await_resume() {
    return callerData.returnVal;
  }

  sendAwaiter(int argFd, const void *argBuf, size_t argLen, int argFlags,
              uringInstance &targetRing)
      : fd(argFd), buf(argBuf), len(argLen), flags(argFlags),
        uring(targetRing) {}

  int fd;
  const void *buf;
  size_t len;
  int flags;
  uringInstance &uring;
  uringInstance::userData callerData;
};

struct recvAwaiter {
  bool await_ready() { return false; }
  bool await_suspend(std::coroutine_handle<> coro) {
    callerData.handle = coro;
    callerData.multishot = false;
    auto addRes = uring.prep_recv(fd, buf, len, flags, &callerData);
    if (!addRes) {
      callerData.returnVal = tl::unexpected(addRes.error());
      return false;
    }
    return true;
  }

  tl::expected<int, std::error_code> await_resume() {
    return callerData.returnVal;
  }

  recvAwaiter(int argFd, void *argBuf, size_t argLen, int argFlags,
              uringInstance &targetRing)
      : fd(argFd), buf(argBuf), len(argLen), flags(argFlags),
        uring(targetRing) {}

  int fd;
  void *buf;
  size_t len;
  int flags;
  uringInstance &uring;
  uringInstance::userData callerData;
};

struct multishotAcceptAwaiter {
  bool await_ready() { return false; }
  bool await_suspend(std::coroutine_handle<> coro) {
    callerData.handle = coro;
    callerData.multishot = true;
    callerData.multishotHandler = multishotHandler;
    auto addRes = uring.prep_multishot_accept_and_process(fd, nullptr, nullptr,
                                                          0, &callerData);
    if (!addRes) {
      callerData.returnVal = tl::unexpected(addRes.error());
      return false;
    }
    return true;
  }

  tl::expected<int, std::error_code> await_resume() {
    return callerData.returnVal;
  }

  multishotAcceptAwaiter(int file, std::function<Task<>(int)> handler,
                         uringInstance &targetRing)
      : fd(file), multishotHandler(handler), uring(targetRing) {}

  int fd;
  std::function<Task<>(int)> multishotHandler;
  uringInstance &uring;
  uringInstance::userData callerData;
};

// TODO: implement all op awaiters
// recv
// send
struct asyncSocket : socketBase {

  auto send(const void *buf, size_t len, int flags, uringInstance &uring) {
    return sendAwaiter(fd, buf, len, flags, uring);
  }

  auto recv(void *buf, size_t len, int flags, uringInstance &uring) {
    return recvAwaiter(fd, buf, len, flags, uring);
  }

  asyncSocket(int fd) : socketBase(fd) {}

  asyncSocket(asyncSocket &&other) : socketBase(std::move(other)) {}
  asyncSocket &operator=(asyncSocket &&other) {
    socketBase::operator=(std::move(other));
    return *this;
  }

  asyncSocket(asyncSocket &) = delete;
  asyncSocket &operator=(asyncSocket &) = delete;

  bool closed = false;
};

// use multishot to process sockets
inline Task<> asyncAccept(std::unique_ptr<serverSocket> server,
                          std::function<Task<>(int)> handler,
                          uringInstance &uring) {
  // helper awaiter
  debug("Ready to accept");
  while (true) {
    auto acceptRes =
        co_await multishotAcceptAwaiter(server->fd, handler, uring);

    if (acceptRes ||
        acceptRes.error() ==
            make_error_code(std::errc::operation_would_block) ||
        acceptRes.error() ==
            make_error_code(std::errc::resource_unavailable_try_again) ||
        acceptRes.error() == make_error_code(uringErr::sqeBusy) ||
        acceptRes.error() == make_error_code(std::errc::operation_canceled)) {
      continue;
    } else {
      errorlog("Failed to accept: {}, {}", acceptRes.error().category().name(),
               acceptRes.error().message());
      throw std::system_error(acceptRes.error());
    }
  }
}

} // namespace ACPAcoro

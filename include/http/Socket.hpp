#pragma once

#include "async/Epoll.hpp"
#include "async/Tasks.hpp"
#include "tl/expected.hpp"
#include "utils.hpp/ErrorHandle.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <oneapi/tbb/concurrent_hash_map.h>
#include <print>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>

namespace ACPAcoro {

class eofException : public std::runtime_error {
public:
  eofException() : std::runtime_error("EOF") {}
};

enum class socketError {
  success = 0,
  readError,
  writeError,
  recvError,
  sendError,
  eofError,
  closedSocket,
};

inline auto const &socketErrorCode() {
  static struct socketErrorCategory : public std::error_category {
    char const *name() const noexcept override { return "socketError"; }

    std::string message(int c) const override {
      switch (static_cast<socketError>(c)) {
      case socketError::success:
        return "Success";
      case socketError::readError:
        return "Read error";
      case socketError::writeError:
        return "Write error";
      case socketError::recvError:
        return "Recv error";
      case socketError::sendError:
        return "Send error";
      case socketError::eofError:
        return "EOF";
      default:
        return "Unknown error";
      }
    }
  } instance;
  return instance;
}

inline std::error_code make_error_code(socketError e) {
  return {static_cast<int>(e), socketErrorCode()};
}

inline std::error_condition make_error_condition(socketError e) {
  return {static_cast<int>(e), socketErrorCode()};
}

struct socketBase {

  virtual ~socketBase() {
    // std::println("Closing socket {}", fd);
    if (fd >= 0)
      close(fd);
  };

  socketBase &operator=(socketBase &&other) {
    fd = other.fd;
    other.fd = -1;
    return *this;
  }
  socketBase(socketBase &&other) : fd(other.fd) { other.fd = -1; }
  socketBase(int fd) : fd(fd) {}

  socketBase() : fd(-1) {};
  socketBase(socketBase const &) = delete;
  socketBase &operator=(socketBase const &) = delete;

  int fd;
};

struct serverSocket : public socketBase {
  serverSocket(std::string const &port) {
    addrinfo *addrs;
    addrinfo hints = {};
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    checkError(getaddrinfo(NULL, port.c_str(), &hints,
                           &addrs))
        .or_else(throwUnexpected); // TODO: check for errors

    auto sock = checkError(
        socket(addrs->ai_family, addrs->ai_socktype, addrs->ai_protocol));

    fd = sock.or_else(throwUnexpected).value();

    int opt = 1;
    checkError(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                          sizeof(opt)));

    // set socket to non-blocking
    checkError(fcntl(fd, F_SETFL, O_NONBLOCK));

    checkError(bind(fd, addrs->ai_addr, sizeof(*(addrs->ai_addr))));

    freeaddrinfo(addrs);
  }

  int listen(int backlog = 4096) {
    return checkError(::listen(fd, backlog)).or_else(throwUnexpected).value();
  }
};

struct reactorSocket : public socketBase {
  reactorSocket(int fd) : socketBase(fd) {
    // set socket to non-blocking
    checkError(fcntl(fd, F_SETFL, O_NONBLOCK)).or_else(throwUnexpected);
  }
  tl::expected<int, std::error_code> read(char *buffer, int size) {
    if (fd < 0) {
      return tl::unexpected(make_error_code(socketError::readError));
    }
    return checkError(::read(fd, buffer, size));
  }
  tl::expected<int, std::error_code> write(char *buffer, int size) {
    if (fd < 0) {
      return tl::unexpected(make_error_code(socketError::writeError));
    }
    return checkError(::write(fd, buffer, size));
  }
  tl::expected<int, std::error_code> recv(char *buffer, int size) {
    if (fd < 0) {
      return tl::unexpected(make_error_code(socketError::recvError));
    }
    return checkError(::recv(fd, buffer, size, 0));
  }
  tl::expected<int, std::error_code> send(char const *buffer, int size) {
    if (fd < 0) {
      return tl::unexpected(make_error_code(socketError::sendError));
    }
    return checkError(::send(fd, buffer, size, 0));
  }
  tl::expected<int, std::error_code> sendfile(int in_fd, size_t count) {
    if (fd < 0) {
      return tl::unexpected(make_error_code(socketError::sendError));
    }

    return checkError(::sendfile(fd, in_fd, nullptr, count));
  }

  reactorSocket(reactorSocket &&other) : socketBase(std::move(other)) {}

  // static tbb::concurrent_hash_map<std::weak_ptr<reactorSocket>,
  //                                 std::chrono::system_clock::time_point>
  //     timeoutTable;
};

using handlerType = std::function<tl::expected<void, std::error_code>(
    std::shared_ptr<reactorSocket>)>;

// handler returning negative value means that the socket should be closed
inline Task<int, yieldPromiseType<int>>
handleSocket(std::shared_ptr<reactorSocket> sock, handlerType handler) {
  while (true) {
    auto opterror = handler(sock);

    // if the handler returns an error, handle it
    if (!opterror) {
      if (opterror.error() == make_error_code(socketError::eofError)) {

        co_return {};

      } else {
        throw std::system_error(opterror.error());
      }
    }

    // epoll_event event;
    // event.events   = EPOLLIN | EPOLLONESHOT;
    // event.data.ptr = (co_await getSelfAwaiter()).address();
    // epollInstance::getInstance().modifyEvent(sock->fd, &event);

    co_yield {};
  }
}

inline Task<int, yieldPromiseType<int>> acceptAll(serverSocket &server,
                                                  handlerType handler) {
  sockaddr_storage addr;
  socklen_t addrlen = sizeof(addr);

  while (true) {
    int clientfd = accept(server.fd, (sockaddr *)&addr, &addrlen);
    if (clientfd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // epoll_event event;
        // event.events   = EPOLLIN | EPOLLONESHOT;
        // event.data.ptr = (co_await getSelfAwaiter()).address();
        // epollInstance::getInstance().modifyEvent(server.fd, &event);
        co_yield {};
        continue;
      } else {
        throw std::error_code(errno, std::system_category());
      }
    }
    auto client = std::make_shared<reactorSocket>(clientfd);
    // std::println("Accepted connection on fd {}", clientfd);

    auto task = handleSocket(client, handler);
    epoll_event event;
    event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    event.data.ptr = task.detach().address();

    epollInstance::getInstance()
        .addEvent(clientfd, &event)
        .or_else([](auto const &e) {
          std::println("Error adding event: {}", e.message());
          std::terminate();
        });
  }
  co_return {};
};

} // namespace ACPAcoro

namespace std {
template <> struct hash<ACPAcoro::reactorSocket> {
  size_t operator()(ACPAcoro::reactorSocket const &s) const {
    return std::hash<int>{}(s.fd);
  }
};

} // namespace std

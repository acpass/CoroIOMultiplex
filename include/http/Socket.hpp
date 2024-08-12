#pragma once

#include "async/Epoll.hpp"
#include "async/Loop.hpp"
#include "async/Tasks.hpp"
#include "utils.hpp/BufferPool.hpp"
#include "utils.hpp/ErrorHandle.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <print>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>

namespace ACPAcoro {

class eofException : public std::runtime_error {
public:
  eofException() : std::runtime_error("EOF") {}
};

struct socketBase {

  virtual ~socketBase() {
    if (fd >= 0)
      close(fd);
  };

  socketBase &operator=(socketBase &&other) {
    fd       = other.fd;
    other.fd = -1;
    return *this;
  }
  socketBase(socketBase &&other) : fd(other.fd) { other.fd = -1; }
  socketBase(int fd) : fd(fd) {}

  socketBase() : fd(-1) {};
  socketBase(socketBase const &)            = delete;
  socketBase &operator=(socketBase const &) = delete;

  int fd;
};

struct serverSocket : public socketBase {
  serverSocket(std::string const &port) {
    addrinfo *addrs;
    addrinfo hints = {};
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    checkError(getaddrinfo(NULL,
                           port.c_str(),
                           &hints,
                           &addrs)); // TODO: check for errors

    fd = checkError(
        socket(addrs->ai_family, addrs->ai_socktype, addrs->ai_protocol));

    int opt = 1;
    checkError(setsockopt(
        fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)));

    // set socket to non-blocking
    checkError(fcntl(fd, F_SETFL, O_NONBLOCK));

    checkError(bind(fd, addrs->ai_addr, sizeof(*(addrs->ai_addr))));

    freeaddrinfo(addrs);
  }

  int listen(int backlog = 4096) { return checkError(::listen(fd, backlog)); }
};

struct reactorSocket : public socketBase {
  reactorSocket(int fd) : socketBase(fd) {
    // set socket to non-blocking
    checkError(fcntl(fd, F_SETFL, O_NONBLOCK));
  }
  int read(char *buffer, int size) {
    return checkError(::read(fd, buffer, size));
  }
  int write(char *buffer, int size) {
    return checkError(::write(fd, buffer, size));
  }
  int recv(char *buffer, int size) {
    return checkError(::recv(fd, buffer, size, 0));
  }
  int send(char *buffer, int size) {
    return checkError(::send(fd, buffer, size, 0));
  }
  reactorSocket(reactorSocket &&other) : socketBase(std::move(other)) {}
};

// handler returning negative value means that the socket should be closed
inline Task<int, yieldPromiseType<int>>
handleSocket(std::shared_ptr<reactorSocket> sock,
             std::function<void(std::shared_ptr<reactorSocket>)> handler) {
  while (true) {
    try {
      handler(sock);

    } catch (eofException const &) {
      try {
        epollInstance::getInstance().deleteEvent(sock->fd);
      } catch (std::error_code const &e) {}
      co_return {};
    } catch (bufferRunOut const &) {
    } catch (...) {
      std::rethrow_exception(std::current_exception());
    }

    epoll_event event;
    event.events   = EPOLLIN | EPOLLONESHOT;
    event.data.ptr = (co_await getSelfAwaiter()).address();
    epollInstance::getInstance().modifyEvent(sock->fd, &event);

    co_yield {};
  }
}

inline Task<int, yieldPromiseType<int>>
acceptAll(serverSocket &server,
          std::function<void(std::shared_ptr<reactorSocket>)> handler) {
  sockaddr_storage addr;
  socklen_t addrlen = sizeof(addr);

  while (true) {
    int clientfd = accept(server.fd, (sockaddr *)&addr, &addrlen);
    if (clientfd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        co_yield {};
        continue;
      } else {
        throw std::error_code(errno, std::system_category());
      }
      checkError(clientfd);
    }
    auto client = std::make_shared<reactorSocket>(clientfd);

    auto task   = handleSocket(client, handler);
    epoll_event event;
    event.events   = EPOLLIN | EPOLLONESHOT;
    event.data.ptr = task.detach().address();
    try {
      epollInstance::getInstance().addEvent(clientfd, &event);
    } catch (std::error_code const &e) {
      std::println("Error: {}", e.message());
      std::terminate();
    }
  }
  co_return 1;
};

} // namespace ACPAcoro
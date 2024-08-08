#pragma once

#include "async/Loop.hpp"
#include "async/Tasks.hpp"

#include <coroutine>
#include <ranges>
#include <sys/epoll.h>

namespace ACPAcoro {

struct epollInstance {

  int addEvent(int fd, epoll_event *event) {
    return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, event);
  }

  int modifyEvent(int fd, epoll_event *event) {
    return epoll_ctl(epfd, EPOLL_CTL_MOD, fd, event);
  }

  int deleteEvent(int fd) {
    return epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
  }

  void operator=(epollInstance &&) = delete;

  int epfd                         = epoll_create1(0);
  static constexpr int maxevents   = 64;
};

// Wait for an event to occur on the epoll instance and add the task to the loop
inline Task<int, yieldPromiseType<int>> epollWaitEvent(epollInstance &epoll,
                                                       int timeout = -1) {
  auto &loop = loopInstance::getInstance();
  static epoll_event events[epollInstance::maxevents];
  while (true) {
    int fds = epoll_wait(epoll.epfd, events, epollInstance::maxevents, timeout);
    for (auto i : std::ranges::views::iota(0, fds)) {
      loop.addTask(*static_cast<std::coroutine_handle<> *>(events[i].data.ptr));
    }
    co_yield fds;
  }
  co_return -1;
}

} // namespace ACPAcoro
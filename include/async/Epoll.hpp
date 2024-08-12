#pragma once

#include "async/Loop.hpp"
#include "async/Tasks.hpp"
#include "utils.hpp/ErrorHandle.hpp"

#include <coroutine>
#include <print>
#include <ranges>
#include <sys/epoll.h>

namespace ACPAcoro {

struct epollInstance {

  static epollInstance &getInstance() {
    static epollInstance instance;
    return instance;
  }

  int addEvent(int fd, epoll_event *event) {
    // std::println("addEvent: fd: {}", fd);
    return checkError(epoll_ctl(epfd, EPOLL_CTL_ADD, fd, event));
  }

  int modifyEvent(int fd, epoll_event *event) {
    return checkError(epoll_ctl(epfd, EPOLL_CTL_MOD, fd, event));
  }

  int deleteEvent(int fd) {
    return checkError(epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr));
  }

  void operator=(epollInstance &&) = delete;

  int epfd                         = epoll_create1(0);
  static constexpr int maxevents   = 64;

private:
  epollInstance() = default;
};

// Wait for an event to occur on the epoll instance and add the task to the loop
inline Task<int, yieldPromiseType<int>>
epollWaitEvent(int timeout = -1, bool autoRefresh = false) {
  auto &epoll = epollInstance::getInstance();
  auto &loop  = loopInstance::getInstance();
  static epoll_event events[epollInstance::maxevents];
  while (true) {

    int fds = epoll_wait(epoll.epfd, events, epollInstance::maxevents, timeout);
    for (auto i : std::ranges::views::iota(0, fds)) {
      loop.addTask(std::coroutine_handle<>::from_address(events[i].data.ptr));
    }
    if (autoRefresh) {
      loop.addTask(co_await getSelfAwaiter());
    }
    co_yield fds;
  }
  throw std::runtime_error("epollWaitEvent exited");
  co_return -1;
}

} // namespace ACPAcoro
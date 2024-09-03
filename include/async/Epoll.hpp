#pragma once

#include "async/Loop.hpp"
#include "async/Tasks.hpp"
#include "utils.hpp/ErrorHandle.hpp"

#include <print>
#include <ranges>
#include <sys/epoll.h>

namespace ACPAcoro {

struct epollInstance {

  static epollInstance &getInstance() {
    static epollInstance instance;
    return instance;
  }

  static epollInstance &getThreadInstance() {
    thread_local epollInstance instance;
    return instance;
  }

  auto addEvent(int fd, epoll_event *event) {
    // std::println("addEvent: fd: {}", fd);
    return checkError(epoll_ctl(epfd, EPOLL_CTL_ADD, fd, event));
  }

  auto modifyEvent(int fd, epoll_event *event) {
    return checkError(epoll_ctl(epfd, EPOLL_CTL_MOD, fd, event));
  }

  auto deleteEvent(int fd) {
    return checkError(epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr));
  }

  void operator=(epollInstance &&) = delete;

  int epfd = epoll_create1(0);
  static constexpr int maxevents = 64;

private:
  epollInstance() = default;
};

// Wait for an event to occur on the epoll instance and add the task to the loop
inline Task<int, yieldPromiseType<int>> epollWaitEvent(int timeout = -1) {
  auto &epoll = epollInstance::getInstance();
  auto &loop = loopInstance::getInstance();
  epoll_event events[epollInstance::maxevents];
  while (true) {
    // std::println("start epollWaitEvent");

    int fds = epoll_wait(epoll.epfd, events, epollInstance::maxevents, timeout);

    for (auto i : std::ranges::views::iota(0, fds)) {
      // std::println("epollWaitEvent: fd: {}", events[i].data.fd);
      // std::println("epollWaitEvent: events: {}", events[i].events);
      loop.addTask(std::coroutine_handle<>::from_address(events[i].data.ptr),
                   true);
    }
    // std::println("finished epollWaitEvent");
    co_yield {};
  }
  throw std::runtime_error("epollWaitEvent exited");
  co_return {};
}

} // namespace ACPAcoro

#pragma once

#include "async/Loop.hpp"
#include "async/Tasks.hpp"

#include <chrono>
#include <coroutine>
namespace ACPAcoro {

struct timerAwaiter {
  auto await_ready() const noexcept -> bool {
    return time < std::chrono::system_clock::now();
  }

  auto await_suspend(std::coroutine_handle<> coro) const noexcept
      -> std::coroutine_handle<> {
    if (callBackCoro != nullptr) {
      coro = callBackCoro;
    }
    // loopInstance::getInstance().addTimer(coro, time);
    return std::noop_coroutine();
  }

  void await_resume() const noexcept {}

  std::coroutine_handle<> callBackCoro = nullptr;
  std::chrono::system_clock::time_point time;
};

Task<> sleepUntil(std::chrono::system_clock::time_point,
                  std::coroutine_handle<> = nullptr);
Task<> sleepFor(std::chrono::system_clock::duration,
                std::coroutine_handle<> = nullptr);

} // namespace ACPAcoro

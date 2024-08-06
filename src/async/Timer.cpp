#include "async/Timer.hpp"

#include "async/Tasks.hpp"

#include <chrono>
#include <print>

namespace ACPAcoro {

Task<> sleepUntil(std::chrono::system_clock::time_point time,
                  std::coroutine_handle<> coro) {
  co_await timerAwaiter{coro, time};
}

Task<> sleepFor(std::chrono::system_clock::duration duration,
                std::coroutine_handle<> coro) {
  std::println("ready to sleep for {} seconds",
               std::chrono::duration_cast<std::chrono::seconds>(duration));
  co_await timerAwaiter{co_await getSelfAwaiter{},
                        std::chrono::system_clock::now() + duration};
  if (coro)
    coro.resume();
  std::println("slept for {} seconds",
               std::chrono::duration_cast<std::chrono::seconds>(duration));
}
} // namespace ACPAcoro
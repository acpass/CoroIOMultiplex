#include "async/Timer.hpp"

#include "async/Tasks.hpp"

#include <chrono>
#include <print>

namespace ACPAcoro {

Task<> sleepUntil(std::chrono::system_clock::time_point time) {
  co_await timerAwaiter{time};
}

Task<> sleepFor(std::chrono::system_clock::duration duration) {
  std::println("ready to sleep for {} seconds", duration.count());
  co_await timerAwaiter{std::chrono::system_clock::now() + duration};
  std::println("slept for {} seconds", duration.count());
}
} // namespace ACPAcoro
#include "async/Loop.hpp"
#include "async/Tasks.hpp"
#include "async/Timer.hpp"

#include <chrono>
#include <print>
using namespace ACPAcoro;
using namespace std::chrono_literals;

ACPAcoro::Task<int> get_value() {
  co_return 42;
}

Task<> hello() {
  std::println("Hello");
  co_return;
}

ACPAcoro::Task<> co_main() {
  auto value = co_await get_value();
  std::println("get_value() returned: {}", value);

  auto sleeptask1 = sleepFor(1s, hello().detach());
  auto sleeptask2 = sleepFor(2s, hello().detach());
  loopInstance::getInstance().addTask(sleeptask1);
  loopInstance::getInstance().addTask(sleeptask2);

  loopInstance::getInstance().addTask(sleepFor(5s, hello().detach()).detach());

  loopInstance::getInstance().runAll();
  co_return;
}

int main() {
  auto task = co_main();
  std::println("co_main() started");
  task.resume();
  std::println("co_main() ended");
  return 0;
}
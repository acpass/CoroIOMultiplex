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

ACPAcoro::Task<> co_main() {
  auto value = co_await get_value();
  std::println("get_value() returned: {}", value);

  std::println("ready to sleep for one second");
  auto sleeptask1 = sleepFor(1s);
  auto sleeptask2 = sleepFor(2s);
  loopInstance::getInstance().addTask(sleeptask1);
  loopInstance::getInstance().addTask(sleeptask2);
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
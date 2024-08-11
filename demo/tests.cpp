#include "async/Loop.hpp"
#include "async/Tasks.hpp"
#include "async/Timer.hpp"
#include "async/WhenAll.hpp"

#include <chrono>
#include <print>
using namespace ACPAcoro;
using namespace std::chrono_literals;

Task<int, yieldPromiseType<int>> generator() {
  for (int i = 0; i < 10; i++) {
    co_yield i;
    co_await sleepFor(1s);
  }
  co_return 10;
}

Task<int, yieldPromiseType<int>> run3times() {
  for (int i = 0; i < 3; i++) {

    std::println("run3times() yielded: {}", i);
    co_yield i;
    co_await sleepFor(1s);
  }
  std::println("run3times() returned: {}", 3);
  co_return 3;
}

ACPAcoro::Task<int> get_value() {
  co_return 42;
}

Task<int> get_42() {
  co_return 42;
}

Task<double> get_3_14() {
  co_return 3.14;
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

  auto [a, b, voidret] = co_await whenAll(get_3_14(), get_42(), sleepFor(1s));

  std::println("get_42() returned: {}", a);
  std::println("get_3_14() returned: {}", b);

  auto sleeptask3 = sleepFor(3s, hello().detach());
  auto sleeptask4 = sleepFor(4s, hello().detach());

  co_await whenAll(sleeptask3, sleeptask4);

  co_await whenAll(sleepFor(1s), sleepFor(2s));

  auto gen = generator();
  while (!gen.selfCoro.done()) {
    auto i = co_await gen;
    std::println("generator() returned: {}", i);
  }

  auto gen2 = run3times();
  while (!gen2.selfCoro.done()) {
    loopInstance::getInstance().addTask(gen2);
    loopInstance::getInstance().runAll();
  }

  co_return;
}

int main() {
  auto task = co_main();
  std::println("co_main() started");
  loopInstance::getInstance().addTask(task);
  while (!task.selfCoro.done()) {
    loopInstance::getInstance().runAll();
  }

  std::println("co_main() ended");
  return 0;
}
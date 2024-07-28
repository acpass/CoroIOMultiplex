#include "async/Tasks.hpp"

#include <print>
using namespace ACPAcoro;

ACPAcoro::Task<int> get_value() {
  co_return 42;
}

ACPAcoro::Task<> co_main() {
  auto value = co_await get_value();
  std::println("get_value() returned: {}", value);
  co_return;
}

int main() {
  auto task = co_main();
  std::println("co_main() started");
  task.resume();
  std::println("co_main() ended");
  return 0;
}
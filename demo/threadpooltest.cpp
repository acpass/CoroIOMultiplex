#include "async/Loop.hpp"
#include "async/Tasks.hpp"
#include "utils/DEBUG.hpp"
#include <print>

using namespace ACPAcoro;

Task<> co_main(threadPool &pool) {
  debug("start");

  for (int i = 1; i < 11; i++) {
    std::println("From {}: {}", std::this_thread::get_id(), i);
    co_await pool.scheduler;
  }

  co_return;
}

int main() {

  debug("main start");

  threadPool &pool = threadPool::getInstance();
  debug("main get pool");

  for (int i = 0; i < 2; i++) {
    pool.addTask(co_main(pool).detach());
  }
  pool.enter();
}

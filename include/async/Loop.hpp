#pragma once

#include <condition_variable>
#include <coroutine>
#include <deque>
#include <mutex>

namespace ACPAcoro {

class loopInstance {
public:
  static loopInstance &getInstance() {
    thread_local loopInstance instance;
    return instance;
  }

  std::deque<std::coroutine_handle<>> readyTasks;

  void addTask(std::coroutine_handle<> task) { readyTasks.push_back(task); }

  void runAll() {
    while (!readyTasks.empty()) {
      auto task = readyTasks.back();
      readyTasks.pop_back();
      task.resume();
    }
  }

  // for multithread loop, implement later
  std::mutex queueLock;
  std::condition_variable queueCond;
};

} // namespace ACPAcoro
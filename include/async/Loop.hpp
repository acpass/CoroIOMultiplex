#pragma once

#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <functional>
#include <mutex>
#include <queue>
#include <vector>

namespace ACPAcoro {

class loopInstance {
public:
  static loopInstance &getInstance() {
    thread_local loopInstance instance;
    return instance;
  }

  struct timerEvent {
    std::coroutine_handle<> task;
    std::chrono::time_point<std::chrono::system_clock> time;

    bool operator>(timerEvent const &other) const { return time > other.time; }
  };

  std::deque<std::coroutine_handle<>> readyTasks;

  std::priority_queue<timerEvent,
                      std::vector<timerEvent>,
                      std::greater<timerEvent>>
      timerEvents;

  void addTask(std::coroutine_handle<> task) { readyTasks.push_back(task); }
  void addTimer(std::coroutine_handle<> task,
                std::chrono::time_point<std::chrono::system_clock> time) {
    timerEvents.push({task, time});
  }

  void runAll() {
    while (!readyTasks.empty() || !timerEvents.empty()) {
      while (!readyTasks.empty()) {
        auto task = readyTasks.back();
        readyTasks.pop_back();
        task.resume();
      }

      while (!timerEvents.empty() &&
             timerEvents.top().time <= std::chrono::system_clock::now()) {
        auto task = timerEvents.top().task;
        timerEvents.pop();
        task.resume();
      }
      if (readyTasks.empty() && timerEvents.empty()) {
        break;
      }
    }
  }

  void runOne() {
    if (!readyTasks.empty()) {
      auto task = readyTasks.back();
      readyTasks.pop_back();
      task.resume();
    } else if (!timerEvents.empty() &&
               timerEvents.top().time <= std::chrono::system_clock::now()) {
      auto task = timerEvents.top().task;
      timerEvents.pop();
      task.resume();
    }
  }

  // for multithread loop, implement later
  std::mutex queueLock;
  std::condition_variable queueCond;
};

} // namespace ACPAcoro
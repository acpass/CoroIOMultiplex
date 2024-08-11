#pragma once

#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

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
    bool operator<(timerEvent const &other) const { return time < other.time; }
  };
  std::unordered_set<std::coroutine_handle<>> runningTasks;
  std::mutex runningTasksMutex;

  std::deque<std::coroutine_handle<>> readyTasks;

  std::mutex readyTasksMutex;
  std::condition_variable readyTasksCV;

  std::deque<std::coroutine_handle<>> highPriorityTasks;
  std::mutex highPriorityTasksMutex;
  std::condition_variable highPriorityTasksCV;

  std::multiset<timerEvent, std::less<timerEvent>> timerEvents;
  std::mutex timerEventsMutex;
  std::condition_variable timerEventsCV;

  enum class runMode {
    includePriority,
    excludePriority,
  };

  void addTask(std::coroutine_handle<> task) { readyTasks.push_back(task); }
  void addTimer(std::coroutine_handle<> task,
                std::chrono::time_point<std::chrono::system_clock> time) {
    timerEvents.emplace(task, time);
  }

  void addHighPriorityTask(std::coroutine_handle<> task) {
    highPriorityTasks.push_back(task);
  }

  void runHighPriority() {
    while (!highPriorityTasks.empty()) {
      std::unique_lock<std::mutex> highPriorityTasksLock(
          highPriorityTasksMutex);
      highPriorityTasksCV.wait(highPriorityTasksLock,
                               [&] { return !highPriorityTasks.empty(); });
      auto task = highPriorityTasks.back();
      highPriorityTasks.pop_back();
      highPriorityTasksLock.unlock();
      highPriorityTasksCV.notify_one();

      std::unique_lock<std::mutex> runningTasksLock(runningTasksMutex);
      if (!runningTasks.contains(task)) {
        try {
          runningTasks.insert(task);
          runningTasksLock.unlock();
          task.resume();

          runningTasksLock.lock();
          runningTasks.erase(task);
          runningTasksLock.unlock();

        } catch (...) {
          throw std::runtime_error("Error in task");
        };
      } else {
        runningTasksLock.unlock();
      }
    }
  }

  void runAll(runMode mode = runMode::excludePriority) {
    if (mode == runMode::includePriority) [[unlikely]] {
      runHighPriority();
    }
    while (!readyTasks.empty() || !timerEvents.empty()) {
      while (!readyTasks.empty()) {
        std::unique_lock<std::mutex> readyTasksLock(readyTasksMutex);
        readyTasksCV.wait(readyTasksLock, [&] { return !readyTasks.empty(); });
        auto task = readyTasks.back();
        readyTasks.pop_back();
        readyTasksLock.unlock();
        readyTasksCV.notify_one();

        std::unique_lock<std::mutex> runningTasksLock(runningTasksMutex);
        if (!runningTasks.contains(task)) {
          try {
            runningTasks.insert(task);
            runningTasksLock.unlock();
            task.resume();

            runningTasksLock.lock();
            runningTasks.erase(task);
            runningTasksLock.unlock();

          } catch (...) {
            throw std::runtime_error("Error in task");
          };
        } else {
          runningTasksLock.unlock();
        }
      }

      while (!timerEvents.empty() &&
             timerEvents.begin()->time <= std::chrono::system_clock::now()) {

        std::unique_lock<std::mutex> timerEventsLock(timerEventsMutex);
        timerEventsCV.wait(timerEventsLock, [&] {
          return !timerEvents.empty() &&
                 timerEvents.begin()->time <= std::chrono::system_clock::now();
        });
        auto task = timerEvents.begin()->task;
        if (task.done()) [[unlikely]] {
          timerEvents.erase(timerEvents.begin());
          continue;
        }
        timerEvents.erase(timerEvents.begin());
        timerEventsLock.unlock();
        timerEventsCV.notify_one();
        task.resume();
      }

      if (readyTasks.empty() && timerEvents.empty()) {
        break;
      }
    }
  }

  // void runOne() {
  //   if (!readyTasks.empty()) {
  //     auto task = readyTasks.back();
  //     readyTasks.pop_back();
  //     task.resume();
  //   } else if (!timerEvents.empty() &&
  //              timerEvents.begin()->time <= std::chrono::system_clock::now())
  //              {
  //     auto task = timerEvents.begin()->task;
  //     timerEvents.erase(timerEvents.begin());
  //     task.resume();
  //   }
  // }

  // for multithread loop, implement later
};

} // namespace ACPAcoro
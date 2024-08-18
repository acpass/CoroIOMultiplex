#pragma once

#include <barrier>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <functional>
#include <mutex>
#include <oneapi/tbb/concurrent_hash_map.h>
#include <oneapi/tbb/concurrent_set.h>
#include <oneapi/tbb/detail/_task.h>
#include <print>
#include <set>
#include <stdexcept>
#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_set.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ACPAcoro {

class loopInstance {
public:
  static loopInstance &getInstance() {
    static loopInstance instance;
    return instance;
  }

  struct timerEvent {
    std::coroutine_handle<> task;
    std::chrono::time_point<std::chrono::system_clock> time;

    bool operator>(timerEvent const &other) const { return time > other.time; }
    bool operator<(timerEvent const &other) const { return time < other.time; }
  };

  tbb::concurrent_queue<std::pair<std::coroutine_handle<>, bool>> readyTasks;
  tbb::concurrent_hash_map<std::coroutine_handle<>, int> runningTasks;

  std::mutex tasksMutex;
  std::condition_variable_any tasksCV;

  std::multiset<timerEvent, std::less<timerEvent>> timerEvents;
  std::mutex timerEventsMutex;
  std::condition_variable_any timerEventsCV;

  void addTask(std::coroutine_handle<> task, bool autoRefresh = false) {
    readyTasks.emplace(task, autoRefresh);
  }

  void addTimer(std::coroutine_handle<> task,
                std::chrono::time_point<std::chrono::system_clock> time) {
    std::unique_lock<std::mutex> timerEventsLock(timerEventsMutex);
    timerEvents.emplace(task, time);
    timerEventsLock.unlock();
    timerEventsCV.notify_one();
  }

  void runTasks() {
    while (!readyTasks.empty()) {

      std::pair<std::coroutine_handle<>, bool> task;
      if (!readyTasks.try_pop(task)) {
        continue;
      }

      if (!runningTasks.insert({task.first, 0})) {

        readyTasks.emplace(task);
        continue;
      }

      task.first.resume();

      runningTasks.erase(task.first);

      if (task.second && !task.first.done()) {
        addTask(task.first, true);
      }
    }
  }

  void runAll() {

    while (!readyTasks.empty() || !timerEvents.empty()) {

      runTasks();

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
        timerEventsCV.notify_all();
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
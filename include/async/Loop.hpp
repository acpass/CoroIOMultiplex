#pragma once

#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <functional>
#include <mutex>
#include <print>
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

  std::deque<std::coroutine_handle<>> readyTasks;

  std::mutex tasksMutex;
  std::condition_variable_any tasksCV;

  std::multiset<timerEvent, std::less<timerEvent>> timerEvents;
  std::mutex timerEventsMutex;
  std::condition_variable_any timerEventsCV;

  void addTask(std::coroutine_handle<> task) {
    std::unique_lock<std::mutex> tasksLock(tasksMutex);
    readyTasks.push_front(task);
    tasksLock.unlock();
    tasksCV.notify_one();
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

      std::unique_lock<std::mutex> tasksLock(tasksMutex);
      tasksCV.wait(tasksLock, [&] { return !readyTasks.empty(); });

      auto task = readyTasks.back();
      readyTasks.pop_back();

      if (runningTasks.contains(task)) {
        tasksLock.unlock();
        continue;
      }
      runningTasks.insert(task);
      tasksLock.unlock();

      tasksCV.notify_one();
      task.resume();

      {
        std::lock_guard<std::mutex> runningTasksLock(tasksMutex);
        runningTasks.erase(task);
      }
    }
  }

  void runAll() {

    while (!readyTasks.empty() || !timerEvents.empty()) {
      while (!readyTasks.empty()) {

        std::unique_lock<std::mutex> tasksLock(tasksMutex);
        tasksCV.wait(tasksLock, [&] { return !readyTasks.empty(); });

        auto task = std::move(readyTasks.back());
        readyTasks.pop_back();

        if (runningTasks.contains(task)) {
          tasksLock.unlock();
          continue;
        }
        runningTasks.insert(task);
        tasksLock.unlock();

        tasksCV.notify_one();
        task.resume();

        {
          std::lock_guard<std::mutex> runningTasksLock(tasksMutex);
          runningTasks.erase(task);
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
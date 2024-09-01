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
#include <shared_mutex>
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

  // bool indicate self_refreshing
  tbb::concurrent_queue<std::pair<std::coroutine_handle<>, bool>> readyTasks{};
  // bool indicate end flag
  tbb::concurrent_hash_map<std::coroutine_handle<>, bool> runningTasks{};

  tbb::concurrent_hash_map<std::coroutine_handle<>, bool> doneTasks{};
  std::shared_mutex doneDestructLock;

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
  // TODO: add a big lock to destroy all done tasks in the done task and queue
  void runTasks() {
    // size_t runcount = 0;

    while (!readyTasks.empty()) {
      // if (runcount++ > 1000) {
      //   break;
      // }

      std::pair<std::coroutine_handle<>, bool> task;
      // atomic operation
      // if the queue is empty
      // continue
      if (!readyTasks.try_pop(task)) {
        continue;
      }

      // atomic
      if (!runningTasks.insert({task.first, false})) {

        // if the task is already running
        // put it back to the queue
        readyTasks.emplace(task);
        continue;
      }

      // wrong code

      //  decltype(doneTasks)::accessor accessor;
      //  // check before resume to avoid double resume
      //  if (!doneTasks.find(accessor, task.first)) {
      //    task.first.resume();

      //   if (task.first.done()) {
      //     {
      //       // std::shared_lock<std::shared_mutex> lock(doneDestructLock);
      //       doneTasks.insert({task.first, true});
      //       task.first.destroy();
      //     }
      //     runningTasks.erase(task.first);
      //     continue;
      //   }

      // } else {
      //   accessor.release();
      //   runningTasks.erase(task.first);

      //   continue;
      // }

      // let it go
      // maybe implement a deferred destruction later

      if (!task.first.done()) {
        task.first.resume();
      }

      // check after resume to achieve delayed destruction
      runningTasks.erase(task.first);

      if (task.second) {
        addTask(task.first, true);
      }
    }

    // std::unique_lock<std::shared_mutex> lock(doneDestructLock);
    // for (auto &c : doneTasks) {
    //   c.destroy();
    //   doneTasks.unsafe_erase(c);
    // }
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

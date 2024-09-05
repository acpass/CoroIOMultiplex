#pragma once

#include "utils/DEBUG.hpp"
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <oneapi/tbb/concurrent_hash_map.h>
#include <oneapi/tbb/concurrent_set.h>
#include <oneapi/tbb/concurrent_vector.h>
#include <oneapi/tbb/detail/_task.h>
#include <print>
#include <set>
#include <shared_mutex>
#include <stdexcept>
#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_set.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ACPAcoro {

// TODO: 1. from global loopInstance to thread pool
// TODO: 2. from auto-refreshing to await-style scheduling
//

struct threadPool {

  static threadPool &getInstance() {
    static threadPool instance{};
    return instance;
  }

private:
  struct threadTaskQueue {
    std::deque<std::coroutine_handle<>> tasks{};
    std::mutex mutex{};
    std::condition_variable_any cv{};
  };

  std::list<threadTaskQueue> queueList;
  using queueListIter = decltype(queueList)::iterator;
  queueListIter currentQueue;
  std::mutex queueCounterMutex;
  std::condition_variable_any queueListCV;

  std::shared_mutex cleanWorkMutex;

  std::vector<std::jthread> threads;

  // when a task is added, the adder will check if its already bound to a
  // threadpool if so, it will get the mutex and add it to the queue if not, it
  // will try to get a queue Counter mutex and add it to next and then add the
  // task to the prev queue
  //
  // When clean tasks start, the clean thread will check every binding in the
  // map and move doneTasks to a per thread set.
  // (unordered_map<queueListIter, set<coroutine_handle>>)
  //
  // The clean thread will check every thread queue and remove done tasks, and
  // then destroy them.
  std::unordered_map<std::coroutine_handle<>, queueListIter> taskBinding;
  std::mutex bindingMutex;
  std::condition_variable_any bindingCV;

  struct iterHash {
    auto operator()(const queueListIter &iter) const {
      return std::hash<void *>()(&(*iter));
    }
  };

public:
  threadPool(size_t threadCnt = std::thread::hardware_concurrency() - 1)
      : scheduler(*this) {

    if (!(threadCnt >= 1)) {
      throw std::invalid_argument("Thread count cannot be greater than 0");
    }

    auto threadTask = [&]() {
      // debug("{} start", std::this_thread::get_id());
      auto &queue = queueList.emplace_back();

      while (true) {
        runTasks(queue);
      }
    };

    for (size_t i = 0; i < threadCnt; i++) {
      threads.emplace_back(threadTask);
    }

    currentQueue = queueList.begin();
  }

  void enter() { cleanTasks(); }

  void addTask(std::coroutine_handle<> task) {
    std::shared_lock<decltype(cleanWorkMutex)> cleanLock(cleanWorkMutex);

    queueListIter targetQueue;

    std::unique_lock<decltype(bindingMutex)> bindingLock(bindingMutex);

    if (taskBinding.contains(task)) {
      targetQueue = taskBinding.at(task);
    } else {
      // get the currentQueue and plus one to iter
      std::unique_lock<decltype(queueCounterMutex)> queueCounterLock(
          queueCounterMutex);

      targetQueue = currentQueue;
      currentQueue++;

      if (currentQueue == queueList.end()) [[unlikely]] {
        currentQueue = queueList.begin();
      }
      queueCounterLock.unlock();

      taskBinding[task] = targetQueue;
    }
    bindingLock.unlock();

    // add task to targetQueue
    std::unique_lock<decltype(targetQueue->mutex)> queueLock(
        targetQueue->mutex);

    targetQueue->tasks.emplace_back(task);
    // debug("task add successful");

    queueLock.unlock();
    targetQueue->cv.notify_all();
  }

  // run tasks for once
  void runTasks(threadTaskQueue &taskQueue) {

    // big lock to make sure when the clean thread is running, no task will run
    std::shared_lock<decltype(cleanWorkMutex)> cleanLock(cleanWorkMutex);

    std::unique_lock<decltype(taskQueue.mutex)> lock(taskQueue.mutex);
    if (taskQueue.tasks.empty()) {
      // debug("Queue empty");
      cleanLock.unlock();
      taskQueue.cv.wait(lock, [&] { return !taskQueue.tasks.empty(); });

      // avoid dead lock
      // example:
      //   A addTask add a task to the queue and wake up this thread
      //   this thread acquire the lock and yield because of OS scheduling
      //   now A cleanWork is waken and acquire the cleanLock
      //   then the cleanWork will try to acquire the taskQueue lock
      //   but it's already acquired
      //
      //   IT"S A DEAD LOCK
      //
      // so use a poll method to check if the cleanWork has been started
      if (!cleanLock.try_lock()) {
        return;
      }
    }

    // debug("get a task");

    auto task = taskQueue.tasks.front();
    taskQueue.tasks.pop_front();

    lock.unlock();

    // debug("{} get a task, ready to run", std::this_thread::get_id());

    if (!task.done()) {
      task.resume();
    }

    // debug("task run once");
  }

  void cleanTasks() {
    using namespace std::chrono_literals;

    while (true) {

      std::unique_lock<decltype(cleanWorkMutex)> cleanLock(cleanWorkMutex);

      if (taskBinding.size() < 10000) {
        cleanLock.unlock();
        std::this_thread::sleep_for(30s);
      } else {
        std::unordered_map<queueListIter,
                           std::unordered_set<std::coroutine_handle<>>,
                           iterHash>
            doneTasks{};

        // add done tasks to a per thread map
        // and remove it from taskBinding

        std::erase_if(taskBinding, [&](auto &task) {
          if (task.first.done()) {
            doneTasks[task.second].insert(task.first);
            return true;
          }
          return false;
        });

        // Now, process the done tasks

        for (auto &threadQue : doneTasks) {
          auto &queue = *threadQue.first;
          auto &taskSet = threadQue.second;

          std::unique_lock<std::mutex> queueLock(queue.mutex);
          std::erase_if(queue.tasks, [&](auto &task) {
            if (taskSet.contains(task)) {
              return true;
            }
            return false;
          });

          for (auto &task : taskSet) {
            task.destroy();
          }
        }

        cleanLock.unlock();

      } // cleanWork
    } // while
  }

  struct scheduleAwaiter {
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<> handle) { pool.addTask(handle); }
    void await_resume() {}

    threadPool &pool;
  } scheduler;
}; // threadPool

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

    while (true) {
      // if (runcount++ > 1000) {
      //   break;
      // }

      std::pair<std::coroutine_handle<>, bool> task;
      // atomic operation
      // if the queue is empty
      // continue
      if (!readyTasks.try_pop(task)) {
        break;
      }

      // atomic
      if (!runningTasks.insert({task.first, false})) {

        // if the task is already running
        // put it back to the queue
        readyTasks.emplace(task);
        continue;
      }

      // let it go
      // maybe implement a deferred destruction later

      if (!task.first.done()) {
        task.first.resume();
      }

      runningTasks.erase(task.first);

      if (task.second && !task.first.done()) {
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
//

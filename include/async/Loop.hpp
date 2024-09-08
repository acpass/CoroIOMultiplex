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
#include <pthread.h>
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

  // TODO: this rw mutex does not prefer writer
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

    targetQueue->tasks.push_back(task);
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
        debug("nothing to clean, clean work sleep");
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
        debug("clean up, go sleep");

      } // cleanWork

      std::this_thread::sleep_for(30s);
      debug("clean work wake up");

    } // while
  }

  struct scheduleAwaiter {
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<> handle) {
      // debug("schedule from a coroutine");
      pool.addTask(handle);
    }
    void await_resume() {}

    threadPool &pool;
  } scheduler;
}; // threadPool

} // namespace ACPAcoro
//

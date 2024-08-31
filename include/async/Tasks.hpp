#pragma once

#include "async/Loop.hpp"
#include <atomic>
#include <coroutine>
#include <exception>
#include <print>
#include <stdatomic.h>
#include <stdexcept>
#include <utility>

namespace ACPAcoro {

// Awaiter used to return to previous coroutine
// to implement a stack of coroutines (symmetric coroutines)
struct returnPrevAwaiter {
  bool await_ready() const noexcept { return false; }

  // return to previous coroutine if exists
  // else suspend always (return a noop coroutine)
  auto await_suspend(std::coroutine_handle<>) const noexcept
      -> std::coroutine_handle<> {
    if (detached.test(std::memory_order::relaxed) &&
        ended.test(std::memory_order::relaxed)) {
      // it's the resumer's responsibility to destroy the coroutine
      //
      return std::noop_coroutine();
    }

    if (!detached.test() && prevCoro) {
      return prevCoro;
    } else {
      return std::noop_coroutine();
    }
  }

  void await_resume() const noexcept {}

  returnPrevAwaiter(bool endedFlag, bool detachFlag,
                    std::coroutine_handle<> prevCoro)
      : ended(endedFlag), detached(detachFlag), prevCoro(prevCoro) {}

  std::atomic_flag ended = ATOMIC_FLAG_INIT;
  std::atomic_flag detached = ATOMIC_FLAG_INIT;
  std::coroutine_handle<> prevCoro = nullptr;
};

template <typename T> class promiseType;

template <typename T = void, typename P = promiseType<T>>
class [[nodiscard]] Task {
public:
  using promise_type = P;

  struct taskAwaiter {
    bool await_ready() const noexcept { return false; };

    // return the value of the task
    // else rethrow the exception
    T await_resume() const {
      try {
        return selfCoro.promise().getValue();
      } catch (...) {
        std::rethrow_exception(std::current_exception());
      }
    }

    // store the caller coroutine to the stack
    // and call the task coroutine (selfCoro)
    auto await_suspend(std::coroutine_handle<> callerCoro) const noexcept
        -> std::coroutine_handle<> {
      selfCoro.promise().prevCoro = callerCoro;
      return selfCoro;
    }

    std::coroutine_handle<promise_type> selfCoro = nullptr;
  };

  virtual ~Task() {
    if (selfCoro) {
      selfCoro.destroy();
    }
  }
  Task(std::coroutine_handle<promise_type> coro = nullptr) : selfCoro(coro) {}
  Task(Task const &&other)
      : selfCoro(std::exchange(other.selfCoro, nullptr)) {};

  operator std::coroutine_handle<>() const noexcept { return selfCoro; }

  // if a exception is stored, rethrow it
  // else return a value

  taskAwaiter operator co_await() const noexcept { return {selfCoro}; }

  void resume() const {
    if (selfCoro) {
      selfCoro.resume();
    } else {
      throw std::runtime_error("Task is empty");
    }
  }

  std::coroutine_handle<> detach() {
    selfCoro.promise().detached.test_and_set(std::memory_order::relaxed);
    auto coro = selfCoro;
    selfCoro = nullptr;
    return coro;
  }

  std::coroutine_handle<promise_type> selfCoro = nullptr;
};

class promiseBase {
public:
  using finalAwaiter = returnPrevAwaiter;

  auto initial_suspend() -> std::suspend_always { return {}; };

  auto final_suspend() noexcept -> finalAwaiter {
    return {true, detached.test(std::memory_order::relaxed), prevCoro};
  }

  void operator=(promiseBase const &&) = delete;
  void unhandled_exception() noexcept {
    if (detached.test(std::memory_order::relaxed)) {
      std::rethrow_exception(std::current_exception());
    }
    this->returnException = std::current_exception();
  }

  void getValue() const {
    if (this->returnException) {
      std::rethrow_exception(this->returnException);
    }
  }

  std::exception_ptr returnException = nullptr;
  std::atomic_flag detached = ATOMIC_FLAG_INIT;
  std::coroutine_handle<> prevCoro = nullptr;
};

template <typename T = void> class promiseType : public promiseBase {
public:
  Task<T> get_return_object() noexcept {
    return Task<T>{std::coroutine_handle<promiseType>::from_promise(*this)};
  }

  void return_value(T &&value) { returnValue = std::forward<T>(value); }

  T &getValue() {
    // if there is an exception, rethrow it
    if (this->returnException) {
      std::rethrow_exception(this->returnException);
    }
    return returnValue;
  }

  T returnValue;
};

template <> class promiseType<void> : public promiseBase {

public:
  Task<void> get_return_object() noexcept {
    return Task<void>{std::coroutine_handle<promiseType>::from_promise(*this)};
  }

  void return_void() noexcept {};
};

struct getSelfAwaiter {
  bool await_ready() const noexcept { return false; }

  std::coroutine_handle<> await_resume() { return selfCoro; }

  bool await_suspend(std::coroutine_handle<> callerCoro) noexcept {
    selfCoro = callerCoro;
    return false;
  }

  std::coroutine_handle<> selfCoro = nullptr;
};
// unused code for whenAll

//  struct retPrevPromiseType;

// struct retPrevTask : Task<> {
//   using promise_type = retPrevPromiseType;
//   retPrevTask(std::coroutine_handle<retPrevPromiseType> coro)
//       : selfCoro(coro) {}

//   virtual ~retPrevTask() {
//     if (selfCoro) {
//       selfCoro.destroy();
//     }
//   }

//   std::coroutine_handle<retPrevPromiseType> selfCoro = nullptr;
// };

struct retPrevPromiseType : public promiseBase {
  Task<void, retPrevPromiseType> get_return_object() noexcept {
    return Task<void, retPrevPromiseType>{
        std::coroutine_handle<retPrevPromiseType>::from_promise(*this)};
  }

  void return_value(std::coroutine_handle<> coro) noexcept { prevCoro = coro; }
};

template <typename T> struct yieldPromiseType : public promiseBase {
  Task<T, yieldPromiseType<T>> get_return_object() noexcept {
    return Task<T, yieldPromiseType>{
        std::coroutine_handle<yieldPromiseType>::from_promise(*this)};
  }

  auto yield_value(T v) noexcept {
    returnValue = v;
    return returnPrevAwaiter{false, detached.test(std::memory_order::relaxed),
                             prevCoro};
  }

  T &getValue() {
    if (this->returnException) {
      std::rethrow_exception(this->returnException);
    }
    return returnValue;
  }

  void return_value(T &&v) noexcept {
    ended.test_and_set();
    returnValue = std::forward<T>(v);
  }

  std::atomic_flag ended = ATOMIC_FLAG_INIT;
  T returnValue;
};

} // namespace ACPAcoro

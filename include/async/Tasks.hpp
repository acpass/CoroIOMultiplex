#pragma once

#include <atomic>
#include <coroutine>
#include <exception>
#include <stdexcept>
#include <utility>
#include <variant>

namespace ACPAcoro {

// Awaiter used to return to previous coroutine
// to implement a stack of coroutines (symmetric coroutines)
struct returnPrevAwaiter {
  bool await_ready() const noexcept { return false; }

  // return to previous coroutine if exists
  // else suspend always (return a noop coroutine)
  auto await_suspend(std::coroutine_handle<>) const noexcept
      -> std::coroutine_handle<> {
    if (prevCoro) {
      return prevCoro;
    } else {
      return std::noop_coroutine();
    }
  }

  void await_resume() const noexcept {}

  std::coroutine_handle<> prevCoro = nullptr;
};

template <typename T>
class promiseType;

template <typename T = void>
class [[nodiscard]] Task {
public:
  using promise_type = promiseType<T>;

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
  std::coroutine_handle<promise_type> selfCoro = nullptr;
};

template <typename T = void>
class promiseBase {
public:
  using finalAwaiter = returnPrevAwaiter;

  auto initial_suspend() -> std::suspend_always { return {}; };

  auto final_suspend() noexcept -> finalAwaiter { return {prevCoro}; }

  void operator=(promiseBase const &&) = delete;

  std::coroutine_handle<> prevCoro     = nullptr;
};

template <typename T = void>
class promiseType : public promiseBase<T> {
public:
  Task<T> get_return_object() noexcept {
    return Task<T>{std::coroutine_handle<promiseType>::from_promise(*this)};
  }

  void return_value(T const &value) noexcept(
      std::is_nothrow_copy_constructible_v<T>) {
    returnValue = value;
  }

  void
  return_value(T &&value) noexcept(std::is_nothrow_move_constructible_v<T>) {
    returnValue = std::move(value);
  }

  void unhandled_exception() noexcept {
    returnValue = std::current_exception();
  }

  T &getValue() {
    // if there is an exception, rethrow it
    if (std::holds_alternative<std::exception_ptr>(returnValue)) {
      std::rethrow_exception(std::get<std::exception_ptr>(returnValue));
    }
    return std::get<T>(returnValue);
  }

  std::variant<T, std::exception_ptr> returnValue;
};

template <>
class promiseType<void> : public promiseBase<void> {

public:
  Task<void> get_return_object() noexcept {
    return Task<void>{std::coroutine_handle<promiseType>::from_promise(*this)};
  }

  void return_void() noexcept {};

  void unhandled_exception() noexcept {
    exceptionValue = std::current_exception();
  }

  void getValue() const {
    if (exceptionValue) {
      std::rethrow_exception(exceptionValue);
    }
  }

  std::exception_ptr exceptionValue;
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

// struct retPrevPromiseType : public promiseBase<void> {
//   retPrevTask get_return_object() noexcept {
//     return retPrevTask{
//         std::coroutine_handle<retPrevPromiseType>::from_promise(*this)};
//   }
//   void unhandled_exception() noexcept {
//     std::rethrow_exception(std::current_exception());
//   }

//   void return_value(std::coroutine_handle<> coro) noexcept { prevCoro = coro;
//   }
// };

} // namespace ACPAcoro
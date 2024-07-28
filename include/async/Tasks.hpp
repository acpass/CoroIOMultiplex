#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <variant>
namespace ACPAcoro {

// Awaiter used to return to previous coroutine
// to implement a stack of coroutines (symmetric coroutines)
struct returnPrevAwaiter {
  bool await_ready() noexcept { return false; }

  // return to previous coroutine if exists
  // else suspend always (return a noop coroutine)
  std::coroutine_handle<>
  await_suspend(std::coroutine_handle<> callerCoro) noexcept;

  void await_resume() noexcept {}

  std::coroutine_handle<> prevCoro = nullptr;
};

template <typename T> class promiseType;

template <typename T = void> class Task {
public:
  using promise_type = promiseType<T>;

  struct taskAwaiter {
    bool await_ready() noexcept;

    // return the value of the task
    T await_resume() noexcept;

    // store the caller coroutine to the stack
    // and call the task coroutine (selfCoro)
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> callerCoro) noexcept;
  };

  ~Task();
  T getValue() noexcept;

  taskAwaiter operator co_await() const noexcept;

  std::coroutine_handle<promiseType<T>> selfCoro = nullptr;
};

template <typename T = void> class promiseBase {
public:
  using finalAwaiter = returnPrevAwaiter;

  Task<T> get_return_object();

  std::suspend_always initial_suspend() {};
  void unhandled_exception() noexcept {}

  finalAwaiter final_suspend() noexcept;

  void operator=(const promiseBase &&) = delete;

  std::coroutine_handle<> prevCoro = nullptr;
};

template <typename T = void> class promiseType : public promiseBase<T> {
public:
  returnPrevAwaiter return_value(T value) noexcept;
  void unhandled_exception() noexcept;

  std::variant<T, std::exception_ptr> returnValue;
};

template <> class promiseType<void> : public promiseBase<void> {
  returnPrevAwaiter return_void() noexcept;
};

}; // namespace ACPAcoro
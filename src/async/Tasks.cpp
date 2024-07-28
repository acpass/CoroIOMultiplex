#include "async/Tasks.hpp"

#include <coroutine>
#include <exception>
#include <variant>

int a = 312312 + 31231 - 23131;

namespace ACPAcoro {

std::coroutine_handle<>
returnPrevAwaiter::await_suspend(std::coroutine_handle<>) const noexcept {
  if (prevCoro) {
    return prevCoro;
  } else {
    return std::noop_coroutine();
  }
}

template <typename T>
T Task<T>::taskAwaiter::await_resume() const {
  try {
    return task.getValue();
  } catch (...) {
    std::rethrow_exception(std::current_exception());
  }
};

template <typename T>
Task<T>::~Task() {
  if (selfCoro) {
    selfCoro.destroy();
  }
}

template <typename T>
T &Task<T>::getValue() const {
  if (selfCoro.promise().exception) {
    std::rethrow_exception(selfCoro.promise().exception);
  }
  return selfCoro.promise().value;
}

template <typename T>
Task<T>::taskAwaiter Task<T>::operator co_await() const noexcept {
  return {*this};
}

}; // namespace ACPAcoro
#pragma once

#include <concepts>
#include <coroutine>

namespace ACPAcoro {

template <typename A>
concept Awaiter = requires(A a) {
  { a.await_ready() } -> std::convertible_to<bool>;
  {
    a.await_suspend(std::coroutine_handle<>())
  } -> std::convertible_to<std::coroutine_handle<>>;
  { a.await_resume() };
};

template <typename A>
concept Awaitable = Awaiter<A> || requires(A a) {
  { a.operator co_await() } -> Awaiter;
};

template <typename T>
struct nonVoidHelper {
  using type = T;
};

template <>
struct nonVoidHelper<void> {
  using type = nonVoidHelper;
};

template <Awaitable A>
struct AwaitableTraits;

template <Awaiter A>
struct AwaitableTraits<A> {
  using retType = decltype(std::declval<A>().await_resume());
  using nonVoidRetType =
      nonVoidHelper<decltype(std::declval<A>().await_resume())>::type;
};

template <class A>
  requires(!Awaiter<A> && Awaitable<A>)
struct AwaitableTraits<A>
    : AwaitableTraits<decltype(std::declval<A>().operator co_await())> {};

} // namespace ACPAcoro
/*
uncompleted
*/

#pragma once

// #include "async/Loop.hpp"
// #include "async/Tasks.hpp"
// #include "async/Traits.hpp"

// #include <any>
// #include <coroutine>
// #include <cstddef>
// #include <span>
// #include <type_traits>
// #include <vector>
#include "Traits.hpp"
#include "async/Loop.hpp"
#include "async/Tasks.hpp"

#include <coroutine>
#include <cstddef>
#include <print>
#include <span>
#include <tuple>
#include <utility>

namespace ACPAcoro {

struct whenAllCtlBlock {
  std::size_t count                  = 0;
  std::coroutine_handle<> callerCoro = nullptr;
};

struct whenAllAwaiter {
  auto await_ready() const noexcept -> bool { return false; }
  auto await_suspend(std::coroutine_handle<> callerCoro) const noexcept {
    loopInstance &loop = loopInstance::getInstance();
    for (auto coro : taskCoros) {
      loop.addTask(coro);
    }
    loop.runAll();
    return callerCoro;
  }

  void await_resume() const noexcept {}

  std::span<std::coroutine_handle<>> taskCoros;
};

template <Awaitable A>
Task<void, retPrevPromiseType>
whenAllHelper(A &&task,
              whenAllCtlBlock &ctlBlock,
              typename AwaitableTraits<A>::nonVoidRetType &retVal) {
  std::println("non-void task");
  retVal = co_await task;
  if (--ctlBlock.count == 0) {
    co_return ctlBlock.callerCoro;
  }
  co_return std::noop_coroutine();
}

template <Awaitable A>
Task<void, retPrevPromiseType>
whenAllHelper(A &&task, whenAllCtlBlock &ctlBlock, nonVoidHelper<void>) {
  std::println("void task");
  co_await task;
  if (--ctlBlock.count == 0) {
    co_return ctlBlock.callerCoro;
  }
  co_return std::noop_coroutine();
}

template <std::size_t... Is, typename... Ts>
Task<std::tuple<typename AwaitableTraits<Ts>::nonVoidRetType...>>
whenAllImpl(std::index_sequence<Is...>, Ts &&...tasks) {
  auto result = std::tuple<typename AwaitableTraits<Ts>::nonVoidRetType...>();

  whenAllCtlBlock ctlBlock{sizeof...(Ts), co_await getSelfAwaiter{}};

  std::array<std::coroutine_handle<>, sizeof...(Ts)> taskCoros = {
      whenAllHelper(std::forward<Ts>(tasks), ctlBlock, std::get<Is>(result))
          .detach()...};

  co_await whenAllAwaiter{taskCoros};

  co_return result;
}

template <Awaitable... Ts>
auto whenAll(Ts &&...tasks) {
  return whenAllImpl(std::make_index_sequence<sizeof...(Ts)>(),
                     std::forward<Ts>(tasks)...);
}

// class whenAllCtlBlock {
// public:
//   std::size_t count                  = 0;
//   std::coroutine_handle<> callerCoro = nullptr;
// };

// class whenAllAwaiter {
// public:
//   whenAllAwaiter(std::span<std::coroutine_handle<>> tasks,
//                  std::vector<std::any> &results,
//                  whenAllCtlBlock &ctl)
//       : taskCoros(tasks), taskResults(results), ctlBlock(ctl) {}
//   bool await_ready() const noexcept { return false; }

//   auto await_suspend(std::coroutine_handle<>) const noexcept -> bool {
//     loopInstance &loop = loopInstance::getInstance();
//     for (auto coro : taskCoros) {
//       loop.addTask(coro);
//     }
//     loop.runAll();
//     return false;
//   }

//   void await_resume() const noexcept {}

//   std::span<std::coroutine_handle<>> taskCoros;
//   std::vector<std::any> &taskResults;
//   whenAllCtlBlock &ctlBlock;
// };

// retPrevTask whenAllHelper(std::coroutine_handle<> calleeCoro,
//                           whenAllCtlBlock &ctlBlock);

// template <Awaitable... Ts>
//   requires(sizeof...(Ts) > 1)
// Task<std::vector<std::any>> whenAll(Ts... tasks) {

//   whenAllCtlBlock ctlBlock;

//   ctlBlock.count      = sizeof...(Ts);

//   ctlBlock.callerCoro = co_await getSelfAwaiter{};

//   std::vector<std::any> taskResults;
//   std::array<std::coroutine_handle<>, sizeof...(Ts)> taskCoros = {
//       whenAllHelper(tasks.selfCoro, ctlBlock).detach()...};

//   co_await whenAllAwaiter{taskCoros, ctlBlock};

//   co_return;
// }

} // namespace ACPAcoro
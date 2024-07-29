/*
uncompleted
*/

#pragma once

#include "async/Loop.hpp"
#include "async/Tasks.hpp"
#include "async/Traits.hpp"

#include <any>
#include <coroutine>
#include <cstddef>
#include <span>
#include <type_traits>
#include <vector>

namespace ACPAcoro {

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
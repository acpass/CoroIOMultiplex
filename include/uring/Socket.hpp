#pragma once

// TODO: use co_await style io operation
//   e.g. co_await read(socket, buffer);
//   read will issue a io_uring read operation related with the caller coroutine
//   when the operation is done, the coroutine will be add to scheduler
//   and resume when it's turn
//

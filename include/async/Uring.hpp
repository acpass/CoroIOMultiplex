#pragma once

#include "async/Loop.hpp"
#include "async/Tasks.hpp"
#include "tl/expected.hpp"
#include "utils/DEBUG.hpp"
#include <atomic>
#include <cerrno>
#include <functional>
#include <liburing.h>
#include <liburing/io_uring.h>
#include <mutex>
#include <system_error>
#include <variant>
#include <vector>

// struct io_uring_sqe {
//    __u8 opcode;
//    __u8 flags;
//    __u16 ioprio;
//    __s32 fd;
//    __u64 off;
//    __u64 addr;
//    __u32 len;
//    union {
//    __kernel_rwf_t rw_flags;
//    __u32 fsync_flags;
//    __u16 poll_events;
// __u32 sync_range_flags;
// __u32 msg_flags;
//    };
//    __u64 user_data;
//    union {
//    __u16 buf_index;
//    __u64 __pad2[3];
//    };
// };
//
// struct io_uring_cqe {
//    __u64 user_data;
//    __s32 res;
//    __u32 flags;
// };

/*
 * Library interface to io_uring
 */
// struct io_uring_sq {
// 	unsigned *khead;
// 	unsigned *ktail;
// 	// Deprecated: use `ring_mask` instead of `*kring_mask`
// 	unsigned *kring_mask;
// 	// Deprecated: use `ring_entries` instead of `*kring_entries`
// 	unsigned *kring_entries;
// 	unsigned *kflags;
// 	unsigned *kdropped;
// 	unsigned *array;
// 	struct io_uring_sqe *sqes;
//
// 	unsigned sqe_head;
// 	unsigned sqe_tail;
//
// 	size_t ring_sz;
// 	void *ring_ptr;
//
// 	unsigned ring_mask;
// 	unsigned ring_entries;
//
// 	unsigned pad[2];
// };
//
// struct io_uring_cq {
// 	unsigned *khead;
// 	unsigned *ktail;
// 	// Deprecated: use `ring_mask` instead of `*kring_mask`
// 	unsigned *kring_mask;
// 	// Deprecated: use `ring_entries` instead of `*kring_entries`
// 	unsigned *kring_entries;
// 	unsigned *kflags;
// 	unsigned *koverflow;
// 	struct io_uring_cqe *cqes;
//
// 	size_t ring_sz;
// 	void *ring_ptr;
//
// 	unsigned ring_mask;
// 	unsigned ring_entries;
//
// 	unsigned pad[2];
// };
//
// struct io_uring {
// 	struct io_uring_sq sq;
// 	struct io_uring_cq cq;
// 	unsigned flags;
// 	int ring_fd;
//
// 	unsigned features;
// 	int enter_ring_fd;
// 	__u8 int_flags;
// 	__u8 pad[3];
// 	unsigned pad2;
// };
//
//

namespace ACPAcoro {

enum class uringErr { success = 0, sqeBusy = 1 };

inline auto uringErrCategory() -> const std::error_category & {
  class uringErrCategory : public std::error_category {
  public:
    virtual const char *name() const noexcept override { return "uringErr"; }
    virtual std::string message(int c) const override {
      switch (static_cast<uringErr>(c)) {
      case uringErr::success:
        return "success";
      case uringErr::sqeBusy:
        return "sqe is busy";
      default:
        return "unknown";
      }
    }
  };
  static uringErrCategory category;
  return category;
}

inline std::error_code make_error_code(uringErr e) {
  return {static_cast<int>(e), uringErrCategory()};
}

struct uringInstance {

  static constexpr int MAX_ENTRIES = 1024;

  void operator=(uringInstance &&) = delete;

  uringInstance(threadPool &p) : pool(p) {
    io_uring_params params;
    memset(&params, 0, sizeof(params));
    params.flags |= IORING_SETUP_SQPOLL | IORING_SETUP_CQSIZE;
    params.sq_entries = MAX_ENTRIES;
    params.cq_entries = MAX_ENTRIES * 8;
    params.sq_thread_idle = 10000;

    auto returnVal = io_uring_queue_init_params(MAX_ENTRIES, &uring, &params);
    debug("Uring creaded");
    debug("support features: {:016b}", params.features);
    debug("sq_entries: {}", params.sq_entries);
    debug("cq_entries: {}", params.cq_entries);
    if (returnVal < 0) {
      if (returnVal == -EPERM)
        debug("Failed to initialize uring: Permission denied");
      throw std::runtime_error("Failed to initialize uring");
    }
    uringFd = uring.ring_fd;
  }

  ~uringInstance() { io_uring_queue_exit(&uring); }

  struct userData {
    bool multishot;
    std::coroutine_handle<> handle;
    tl::expected<int, std::error_code> returnVal;
    std::function<Task<>(int)> multishotHandler;
  };

  Task<> reapIOs() {
    debug("Ready to reapIOs");
    while (true) {
      io_uring_cqe *cqe = nullptr;
      int ret = io_uring_peek_cqe(&uring, &cqe);

      if (ret < 0) {
        if (ret == -EAGAIN) {
          if (cqe != nullptr) {
            io_uring_cqe_seen(&uring, cqe);
          }
          co_await pool.scheduler;
          continue;
        } else {
          errorlog("Failed to wait for cqe: {}",
                   std::generic_category().message(-ret));

          throw std::system_error(-ret, std::generic_category());
        }
      } else if (cqe == nullptr) {
        co_await pool.scheduler;
        continue;
      } else {

        auto caller = reinterpret_cast<userData *>(io_uring_cqe_get_data(cqe));

        if (cqe->res < 0) {
          caller->returnVal = tl::unexpected(
              std::error_code(-cqe->res, std::generic_category()));
        } else {
          caller->returnVal = cqe->res;
        }

        if (!caller->multishot) {
          pool.addTask(caller->handle);

          // deal with multishot request
        } else {
          // add a task for each successful request
          if (cqe->res >= 0) {
            pool.addTask(caller->multishotHandler(cqe->res).detach());
          }

          // if it's the last, resume the caller
          if (!(cqe->flags & IORING_CQE_F_MORE)) {
            pool.addTask(caller->handle);
          }
        }

        io_uring_cqe_seen(&uring, cqe);
      }
    }
  }

  tl::expected<void, std::error_code>
  prep_send(int fd, const void *buf, size_t len, int flags, userData *usr) {
    std::scoped_lock<decltype(uringAddMutex)> lock(uringAddMutex);
    io_uring_sqe *sqe = io_uring_get_sqe(&uring);

    if (sqe == nullptr) {
      return tl::unexpected(make_error_code(uringErr::sqeBusy));
    }
    io_uring_sqe_set_data(sqe, usr);

    io_uring_prep_send(sqe, fd, buf, len, flags);

    io_uring_submit(&uring);

    return {};
  }

  tl::expected<void, std::error_code> prep_recv(int fd, void *buf, size_t len,
                                                int flags, userData *usr) {
    std::scoped_lock<decltype(uringAddMutex)> lock(uringAddMutex);
    io_uring_sqe *sqe = io_uring_get_sqe(&uring);

    if (sqe == nullptr) {
      return tl::unexpected(make_error_code(uringErr::sqeBusy));
    }
    io_uring_sqe_set_data(sqe, usr);

    io_uring_prep_recv(sqe, fd, buf, len, flags);

    io_uring_submit(&uring);
    return {};
  }

  tl::expected<void, std::error_code>
  prep_multishot_accept_and_process(int fd, sockaddr *addr, socklen_t *len,
                                    int flags, userData *usr) {
    std::scoped_lock<decltype(uringAddMutex)> lock(uringAddMutex);
    io_uring_sqe *sqe = io_uring_get_sqe(&uring);
    if (sqe == nullptr) {
      return tl::unexpected(make_error_code(uringErr::sqeBusy));
    }
    io_uring_sqe_set_data(sqe, usr);

    io_uring_prep_multishot_accept(sqe, fd, addr, len, flags);

    io_uring_submit(&uring);

    return {};
  }

  // a userData struct should be created by the awaiter
  // and its pointer should be passed to the uring
private:
  std::mutex uringAddMutex;
  threadPool &pool;
  io_uring uring;
  int uringFd;
};

} // namespace ACPAcoro

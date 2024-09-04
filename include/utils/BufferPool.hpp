#pragma once

#include <atomic>
#include <bitset>
#include <cstddef>
#include <print>
#include <span>
#include <stdexcept>
namespace ACPAcoro {

class bufferRunOut : public std::runtime_error {
public:
  bufferRunOut() : std::runtime_error("Buffer run out") {}
};

template <typename T, std::size_t N>
class bufferPool {
public:
  static constexpr std::size_t bufferSize = 1024;

  bufferPool() {
    for (size_t i = 0; i < N; i++) {
      buffers[i] = new T[bufferSize];
    }
  }

  ~bufferPool() {
    for (size_t i = 0; i < N; i++) {
      delete buffers[i];
    }
  }

  std::span<T> getBuffer() {

    for (size_t i = 0; i < N; i++) {

      if (!used[i].test_and_set()) {
        // std::println("getBuffer: {}", i);
        return {buffers[i], bufferSize};
      }
    }

    // std::println("Buffer run out");
    throw bufferRunOut();
  }

  void returnBuffer(std::span<T> buffer) {

    for (size_t i = 0; i < N; i++) {
      if (buffers[i] == buffer.data()) {
        // std::println("returnBuffer: {}", i);
        used[i].clear();
        return;
      }
    }
  }

  constexpr std::size_t size() const { return N; }

private:
  T *buffers[N];

  std::atomic_flag used[N] = {ATOMIC_FLAG_INIT};
};

} // namespace ACPAcoro
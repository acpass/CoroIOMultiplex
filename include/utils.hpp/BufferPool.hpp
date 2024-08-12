#pragma once

#include <cstddef>
namespace ACPAcoro {

template <typename T, std::size_t N>
class bufferPool {
public:
  bufferPool() {
    for (size_t i = 0; i < N; i++) {
      buffers[i] = new T();
    }
  }

  ~bufferPool() {
    for (size_t i = 0; i < N; i++) {
      delete buffers[i];
    }
  }

  T *getBuffer() {
    for (size_t i = 0; i < N; i++) {
      if (!used[i]) {
        used[i] = true;
        return buffers[i];
      }
    }
    return nullptr;
  }

  void returnBuffer(T *buffer) {
    for (size_t i = 0; i < N; i++) {
      if (buffers[i] == buffer) {
        used[i] = false;
        return;
      }
    }
  }

  constexpr std::size_t size() const { return N; }

private:
  T *buffers[N];
  bool used[N] = {false};
};
} // namespace ACPAcoro
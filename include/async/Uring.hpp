#pragma once

namespace ACPAcoro {

struct UringInstance {
  static UringInstance &getInstance() {
    static UringInstance instance;
    return instance;
  }
  static UringInstance &getThreadInstance() {
    thread_local UringInstance instance;
    return instance;
  }

  void operator=(UringInstance &&) = delete;
};

} // namespace ACPAcoro

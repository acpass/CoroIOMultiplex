
#pragma once

#include <system_error>
namespace ACPAcoro {

inline int checkError(int ret) {
  if (ret < 0) {
    throw std::error_code(errno, std::system_category());
  }
  return ret;
}

} // namespace ACPAcoro
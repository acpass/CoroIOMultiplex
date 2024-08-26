
#pragma once

#include "tl/expected.hpp"

#include <system_error>

namespace ACPAcoro {

inline tl::expected<int, std::error_code> checkError(int ret) {
  if (ret < 0) {
    return tl::unexpected(std::error_code(errno, std::system_category()));
  }
  return ret;
}

inline void throwUnexpected(std::error_code const &e) {
  throw std::system_error(e);
}

} // namespace ACPAcoro
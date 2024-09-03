#pragma once

#include "tl/expected.hpp"

#include <fcntl.h>
#include <filesystem>
#include <system_error>
#include <unistd.h>

namespace ACPAcoro {

struct regularFile {

public:
  tl::expected<void, std::error_code> open(std::filesystem::path const &path) {
    if (fd >= 0) {
      ::close(fd);
    }

    fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
      return tl::make_unexpected(
          std::error_code(errno, std::system_category()));
    }

    size = std::filesystem::file_size(path);
    if (size < 0) {
      return tl::make_unexpected(
          std::error_code(errno, std::system_category()));
    }

    return {};
  }

  regularFile() : fd(-1), size(0) {};

  regularFile(regularFile &&other) {
    fd = other.fd;
    size = other.size;
    other.fd = -1;
    other.size = 0;
  };

  ~regularFile() {
    if (fd >= 0)
      ::close(fd);
  }

  int fd;
  size_t size;
};

} // namespace ACPAcoro

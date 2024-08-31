#pragma once

#include "tl/expected.hpp"
#include <fcntl.h>
#include <filesystem>
#include <system_error>
#include <unistd.h>

namespace ACPAcoro {

struct regularFile {

public:
  inline static tl::expected<std::shared_ptr<regularFile>, std::error_code>
  open(std::filesystem::path const &path) {
    auto file = std::make_shared<regularFile>();
    file->fd = ::open(path.c_str(), O_RDONLY);
    if (file->fd < 0) {
      return tl::make_unexpected(
          std::error_code(errno, std::system_category()));
    }
    file->size = std::filesystem::file_size(path);
    if (file->size < 0) {
      return tl::make_unexpected(
          std::error_code(errno, std::system_category()));
    }

    return file;
  }

  ~regularFile() {
    if (fd >= 0)
      ::close(fd);
  }

  size_t size;
  int fd;
};

} // namespace ACPAcoro

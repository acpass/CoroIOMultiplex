#pragma once

#include "file/File.hpp"
#include "utils/Lru.hpp"
#include <filesystem>
#include <span>
#include <sys/mman.h>
namespace ACPAcoro {

struct fileCacheBuilder;

// own the file and mmap memory
struct fileCache {

  friend struct fileCacheBuilder;
  char *data() { return mLoc.data(); }
  size_t size() { return mLoc.size(); }

private:
  fileCache() = default;

  bool map() {
    auto ptr = mmap(nullptr, mFile.size, PROT_READ, MAP_PRIVATE, mFile.fd, 0);
    if (ptr == MAP_FAILED) {
      return false;
    }

    mLoc = std::span<char>(static_cast<char *>(ptr), mFile.size);
    return true;
  }

  std::span<char> mLoc;
  regularFile mFile;
};

struct fileCacheBuilder {
  using wrappedType = std::shared_ptr<fileCache>;
  wrappedType build(std::filesystem::path p) {
    auto fc = std::make_shared<fileCache>();
    if (!fc->mFile.open(p)) {
      return nullptr;
    }
    fc->map();
    return fc;
  }
};

using fileCacheFactory = cacheFactory<std::filesystem::path, fileCacheBuilder>;

} // namespace ACPAcoro

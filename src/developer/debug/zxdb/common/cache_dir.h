// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_CACHE_DIR_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_CACHE_DIR_H_

#include <filesystem>
#include <unordered_map>

namespace zxdb {

// A cache directory automatically removes least recently used files when its size exceeds the
// maximum size. More sophisticated features could be added in the future.
class CacheDir {
 public:
  // Default size for cache directory.
  static constexpr uint64_t kDefaultMaxSize = 8ull * 1024 * 1024 * 1024;

  // Declare a cache directory with the maximum size in bytes. When the size of the cache directory
  // is larger than the max_size_bytes, an LRU pruning will be triggered. A value of 0 disables the
  // cache pruning.
  explicit CacheDir(std::filesystem::path dir, uint64_t max_size_bytes = kDefaultMaxSize);

  // The caller of this class is able to access, create files in the cache directory directly but
  // needs to notify us about the access.
  //
  // If the file is not in the cache_dir, this function does nothing.
  // It's guaranteed that the file won't be deleted by this call.
  void NotifyFileAccess(const std::filesystem::path& file);

 private:
  void PruneDir();

  std::filesystem::path cache_dir_;
  uint64_t max_size_;  // in bytes

  struct FileInfo {
    uint64_t atime = 0;  // in nanoseconds
    uint64_t size = 0;   // in bytes
  };
  std::unordered_map<std::string, FileInfo> file_info_;
  uint64_t total_size_ = 0;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_CACHE_DIR_H_

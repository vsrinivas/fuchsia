// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/cache_dir.h"

#include <fcntl.h>
#include <sys/stat.h>

#include <filesystem>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "lib/syslog/cpp/macros.h"

#if defined(__APPLE__)
#define st_atim st_atimespec
#endif

namespace zxdb {

namespace {

uint64_t TsToNs(const timespec& ts) { return ts.tv_sec * 1000000000 + ts.tv_nsec; }

bool PathStartsWith(const std::filesystem::path& path, const std::filesystem::path& ancestor) {
  auto path_it = path.begin();
  for (auto ancestor_it = ancestor.begin(); ancestor_it != ancestor.end(); ancestor_it++) {
    if (path_it == path.end())
      return false;
    if (*ancestor_it != *path_it)
      return false;
    path_it++;
  }
  return true;
}

}  // namespace

CacheDir::CacheDir(std::filesystem::path dir, uint64_t max_size_bytes)
    : cache_dir_(std::move(dir)), max_size_(max_size_bytes) {
  for (const auto& p : std::filesystem::recursive_directory_iterator(cache_dir_)) {
    std::error_code err;
    if (p.is_regular_file(err)) {
      struct stat file_stat;
      if (!lstat(p.path().c_str(), &file_stat)) {
        file_info_[p.path()].atime = TsToNs(file_stat.st_atim);
        file_info_[p.path()].size = file_stat.st_size;
        total_size_ += file_stat.st_size;
      }
    }
  }

  PruneDir();
}

void CacheDir::NotifyFileAccess(const std::filesystem::path& file) {
  if (!PathStartsWith(file, cache_dir_))
    return;

  if (auto exist = file_info_.find(file); exist != file_info_.end()) {
    timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    // Only touch the access time.
    timespec ts[2] = {now, {0, UTIME_OMIT}};
    utimensat(AT_FDCWD, file.c_str(), ts, 0);
    exist->second.atime = TsToNs(now);
  } else {
    struct stat file_stat;

    if (!lstat(file.c_str(), &file_stat)) {
      file_info_[file].atime = TsToNs(file_stat.st_atim);
      file_info_[file].size = file_stat.st_size;
      total_size_ += file_stat.st_size;

      PruneDir();
    }
  }
}

void CacheDir::PruneDir() {
  if (total_size_ <= max_size_)
    return;

  std::vector<std::pair<std::string, FileInfo>> file_info_v(file_info_.begin(), file_info_.end());
  std::sort(file_info_v.begin(), file_info_v.end(),
            [](auto x, auto y) { return x.second.atime < y.second.atime; });

  // Never remove the last file, regardless of how big it is.
  file_info_v.pop_back();

  for (const auto& [file, info] : file_info_v) {
    std::error_code err;
    std::filesystem::remove(file, err);
    file_info_.erase(file);
    total_size_ -= info.size;

    if (total_size_ <= max_size_)
      break;
  }
}

}  // namespace zxdb

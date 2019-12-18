// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/files/path.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <lib/fit/function.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <functional>
#include <list>
#include <memory>

#include "src/ledger/lib/files/directory.h"

namespace ledger {
namespace {

void SafeCloseDir(DIR* dir) {
  if (dir)
    closedir(dir);
}

bool ForEachEntry(int root_fd, const std::string& path,
                  fit::function<bool(const std::string& path)> callback) {
  int dir_fd = openat(root_fd, path.c_str(), O_RDONLY);
  if (dir_fd == -1) {
    return false;
  }
  std::unique_ptr<DIR, decltype(&SafeCloseDir)> dir(fdopendir(dir_fd), SafeCloseDir);
  if (!dir.get())
    return false;
  for (struct dirent* entry = readdir(dir.get()); entry != nullptr; entry = readdir(dir.get())) {
    char* name = entry->d_name;
    if (name[0]) {
      if (name[0] == '.') {
        if (!name[1] || (name[1] == '.' && !name[2])) {
          // . or ..
          continue;
        }
      }
      if (!callback(path + "/" + name))
        return false;
    }
  }
  return true;
}

}  // namespace

std::string GetDirectoryName(const std::string& path) {
  size_t separator = path.rfind('/');
  if (separator == 0u)
    return "/";
  if (separator == std::string::npos)
    return std::string();
  return path.substr(0, separator);
}

std::string GetBaseName(const std::string& path) {
  size_t separator = path.rfind('/');
  if (separator == std::string::npos)
    return path;
  return path.substr(separator + 1);
}

bool DeletePathAt(int root_fd, const std::string& path, bool recursive) {
  struct stat stat_buffer;
  if (fstatat(root_fd, path.c_str(), &stat_buffer, AT_SYMLINK_NOFOLLOW) != 0)
    return (errno == ENOENT || errno == ENOTDIR);
  if (!S_ISDIR(stat_buffer.st_mode))
    return (unlinkat(root_fd, path.c_str(), 0) == 0);
  if (!recursive)
    return (unlinkat(root_fd, path.c_str(), AT_REMOVEDIR) == 0);

  // Use std::list, as ForEachEntry callback will modify the container. If the
  // container is a vector, this will invalidate the reference to the content.
  std::list<std::string> directories;
  directories.push_back(path);
  for (auto it = directories.begin(); it != directories.end(); ++it) {
    if (!ForEachEntry(root_fd, *it, [root_fd, &directories](const std::string& child) {
          if (IsDirectoryAt(root_fd, child)) {
            directories.push_back(child);
          } else {
            if (unlinkat(root_fd, child.c_str(), 0) != 0)
              return false;
          }
          return true;
        })) {
      return false;
    }
  }
  for (auto it = directories.rbegin(); it != directories.rend(); ++it) {
    if (unlinkat(root_fd, it->c_str(), AT_REMOVEDIR) != 0)
      return false;
  }
  return true;
}

}  // namespace ledger

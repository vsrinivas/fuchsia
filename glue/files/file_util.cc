// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/glue/files/file_util.h"

#include <memory>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/ftl/arraysize.h"
#include "lib/ftl/files/file_descriptor.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/files/unique_fd.h"

namespace glue {

namespace {

void SafeCloseDir(DIR* dir) {
  if (dir)
    closedir(dir);
}

bool ForEachEntry(const std::string& path,
                  std::function<bool(const std::string& path)> callback) {
  std::unique_ptr<DIR, decltype(&SafeCloseDir)> dir(opendir(path.c_str()),
                                                    SafeCloseDir);
  if (!dir.get())
    return false;
  for (struct dirent* entry = readdir(dir.get()); entry != nullptr;
       entry = readdir(dir.get())) {
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

bool GetFileSize(const std::string& path, int64_t* size) {
  struct stat stat_buffer;
  if (stat(path.c_str(), &stat_buffer) != 0) {
    return false;
  }
  *size = stat_buffer.st_size;
  return true;
}

bool IsDirectory(const std::string& path) {
  struct stat buf;
  if (stat(path.c_str(), &buf) != 0) {
    return false;
  }
  return S_ISDIR(buf.st_mode);
}

bool CreateDirectory(const std::string& full_path) {
  std::vector<std::string> subpaths;

  // Collect a list of all parent directories.
  std::string last_path = full_path;
  subpaths.push_back(full_path);
  for (std::string path = files::GetDirectoryName(full_path); path != last_path;
       path = files::GetDirectoryName(path)) {
    subpaths.push_back(path);
    last_path = path;
  }

  // Iterate through the parents and create the missing ones.
  for (auto pathIt = subpaths.rbegin(); pathIt != subpaths.rend(); ++pathIt) {
    if (IsDirectory(*pathIt))
      continue;
    if (mkdir(pathIt->c_str(), 0700) == 0)
      continue;
    // Mkdir failed, but it might be due to the directory appearing out of thin
    // air. This can occur if two processes are trying to create the same file
    // system tree at the same time. Check to see if it exists and make sure it
    // is a directory.
    if (!IsDirectory(*pathIt))
      return false;
  }
  return true;
}

bool DeletePath(const std::string& path, bool recursive) {
  struct stat stat_buffer;
  if (lstat(path.c_str(), &stat_buffer) != 0)
    return (errno == ENOENT || errno == ENOTDIR);
  if (!S_ISDIR(stat_buffer.st_mode))
    return (unlink(path.c_str()) == 0);
  if (recursive)
    return (rmdir(path.c_str()) == 0);

  std::vector<std::string> directories;
  directories.push_back(path);
  for (size_t index = 0; index < directories.size(); ++index) {
    if (!ForEachEntry(directories[index],
                      [&directories](const std::string& child) {
                        if (IsDirectory(child)) {
                          directories.push_back(child);
                        } else {
                          if (unlink(child.c_str()) != 0)
                            return false;
                        }
                        return true;
                      })) {
      return false;
    }
  }
  for (auto it = directories.rbegin(); it != directories.rend(); ++it) {
    if (rmdir(it->c_str()) != 0)
      return false;
  }
  return true;
}

}  // namespace glue

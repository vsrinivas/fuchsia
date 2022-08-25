// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/files/path.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <lib/fit/function.h>
#include <lib/utf-utils/utf-utils.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <functional>
#include <list>
#include <memory>
#include <string_view>

#include "src/lib/files/directory.h"

namespace files {
namespace {

size_t ResolveParentDirectoryTraversal(const std::string& path, size_t put) {
  if (put >= 2) {
    size_t previous_separator = path.rfind('/', put - 2);
    if (previous_separator != std::string::npos)
      return previous_separator + 1;
  }
  if (put == 1 && path[0] == '/') {
    return put;
  }
  return 0;
}

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
  if (!dir)
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
      if (!callback(path + "/" + name)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

std::string SimplifyPath(std::string path) {
  if (path.empty())
    return ".";

  size_t put = 0;
  size_t get = 0;
  size_t traversal_root = 0;
  size_t component_start = 0;

  if (path[0] == '/') {
    put = 1;
    get = 1;
    component_start = 1;
  }

  while (get < path.size()) {
    char c = path[get];

    if (c == '.' && (get == component_start || get == component_start + 1)) {
      // We've seen "." or ".." so far in this component. We need to continue
      // searching.
      ++get;
      continue;
    }

    if (c == '/') {
      if (get == component_start || get == component_start + 1) {
        // We've found a "/" or a "./", which we can elide.
        ++get;
        component_start = get;
        continue;
      }
      if (get == component_start + 2) {
        // We've found a "../", which means we need to remove the previous
        // component.
        if (put == traversal_root) {
          path[put++] = '.';
          path[put++] = '.';
          path[put++] = '/';
          traversal_root = put;
        } else {
          put = ResolveParentDirectoryTraversal(path, put);
        }
        ++get;
        component_start = get;
        continue;
      }
    }

    size_t next_separator = path.find('/', get);
    if (next_separator == std::string::npos) {
      // We've reached the last component.
      break;
    }
    size_t next_component_start = next_separator + 1;
    ++next_separator;
    size_t component_size = next_component_start - component_start;
    if (put != component_start && component_size > 0) {
      path.replace(put, component_size, path.substr(component_start, component_size));
    }
    put += component_size;
    get = next_component_start;
    component_start = next_component_start;
  }

  size_t last_component_size = path.size() - component_start;
  if (last_component_size == 1 && path[component_start] == '.') {
    // The last component is ".", which we can elide.
  } else if (last_component_size == 2 && path[component_start] == '.' &&
             path[component_start + 1] == '.') {
    // The last component is "..", which means we need to remove the previous
    // component.
    if (put == traversal_root) {
      path[put++] = '.';
      path[put++] = '.';
      path[put++] = '/';
      traversal_root = put;
    } else {
      put = ResolveParentDirectoryTraversal(path, put);
    }
  } else {
    // Otherwise, we need to copy over the last component.
    if (put != component_start && last_component_size > 0) {
      path.replace(put, last_component_size, path.substr(component_start, last_component_size));
    }
    put += last_component_size;
  }

  if (put >= 2 && path[put - 1] == '/') {
    --put;  // Trim trailing /
  } else if (put == 0) {
    return ".";  // Use . for otherwise empty paths to treat them as relative.
  }

  path.resize(put);
  return path;
}

std::string AbsolutePath(const std::string& path) {
  if (!path.empty()) {
    if (path[0] == '/') {
      // Path is already absolute.
      return path;
    }
    auto current_directory = GetCurrentDirectory();
    if (current_directory == "/") {
      // No need to prepend "/" if we are relative to the root directory.
      current_directory = "";
    }
    return current_directory + "/" + path;
  }
  // Path is empty.
  return GetCurrentDirectory();
}

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

bool IsValidName(std::string_view name) {
  // * It cannot be longer than [`MAX_NAME_LENGTH`] (255 bytes).
  if (name.length() > 255) {
    return false;
  }
  // * It cannot be empty.
  if (name.empty()) {
    return false;
  }
  // * It cannot be ".." (dot-dot).
  // * It cannot be "." (single dot).
  if (name == ".." || name == ".") {
    return false;
  }
  if (!utfutils_is_valid_utf8(name.data(), name.length())) {
    return false;
  }
  // * It cannot contain "/".
  // * It cannot contain embedded NUL.
  if (std::any_of(name.cbegin(), name.cend(), [](char c) { return c == '/' || c == '\0'; })) {
    return false;
  }
  return true;
}

bool IsValidCanonicalPath(std::string_view path) {
  // * It cannot be empty.
  if (path.empty()) {
    return false;
  }

  // * It cannot be longer than `MAX_PATH_LENGTH` (4095 bytes).
  if (path.length() > 4095) {
    return false;
  }

  // * It cannot have a leading "/".
  if (path.front() == '/') {
    return false;
  }

  // * It cannot have a trailing "/".
  if (path.back() == '/') {
    return false;
  }

  if (!utfutils_is_valid_utf8(path.data(), path.length())) {
    return false;
  }

  // * Each component must be a valid `Name`. See IsValidName().
  while (true) {
    const auto next_slash = path.find('/');
    if (next_slash == std::string_view::npos) {
      return IsValidName(path);
    }
    std::string_view next_segment = path.substr(0, next_slash);
    if (!IsValidName(next_segment)) {
      return false;
    }
    path.remove_prefix(next_slash + 1);
  }
}

bool DeletePath(const std::string& path, bool recursive) {
  return DeletePathAt(AT_FDCWD, path, recursive);
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

std::string JoinPath(const std::string& path1, const std::string& path2) {
  if (path1.empty()) {
    return path2;
  }
  if (path2.empty()) {
    return path1;
  }
  if (path1.back() == '/') {
    if (path2.front() == '/') {
      return path1 + path2.substr(1);
    }
  } else {
    if (path2.front() != '/') {
      return path1 + "/" + path2;
    }
  }
  return path1 + path2;
}

}  // namespace files

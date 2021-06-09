// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/migration/utils/file_utils.h"

#include <lib/fdio/fd.h>
#include <lib/syslog/cpp/macros.h>

#include <queue>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/split_string.h"

namespace forensics::feedback {

fbl::unique_fd IntoFd(::fidl::InterfaceHandle<fuchsia::io::Directory> dir) {
  if (!dir.is_valid()) {
    return fbl::unique_fd(fbl::unique_fd::InvalidValue());
  }

  fbl::unique_fd fd;
  if (const zx_status_t status =
          fdio_fd_create(dir.TakeChannel().release(), fd.reset_and_get_address());
      status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "Failed to convert directory request into file descriptor";
    return fbl::unique_fd(fbl::unique_fd::InvalidValue());
  }

  return fd;
};

::fidl::InterfaceHandle<fuchsia::io::Directory> IntoInterfaceHandle(fbl::unique_fd fd) {
  ::fidl::InterfaceHandle<fuchsia::io::Directory> handle;
  if (!fd.is_valid()) {
    return handle;
  }

  zx::channel channel;
  if (const zx_status_t status = fdio_fd_transfer(fd.release(), channel.reset_and_get_address());
      status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "Failed to transfer file descriptor";
    return handle;
  }

  handle.set_channel(std::move(channel));
  return handle;
}

bool CopyFile(const fbl::unique_fd& source_root_fd, const fbl::unique_fd& sink_root_fd,
              const std::string& relative_path) {
  if (!sink_root_fd.is_valid()) {
    return false;
  }

  if (!source_root_fd.is_valid()) {
    return true;
  }

  std::string content;
  if (!files::ReadFileToStringAt(source_root_fd.get(), relative_path, &content)) {
    FX_LOGS(WARNING) << "Failed to read " << relative_path << " from source root";
    return false;
  }

  // Remove the file name from the path and create the necessary directories.
  std::vector<std::string> path_elements =
      fxl::SplitStringCopy(relative_path, "/", fxl::WhiteSpaceHandling::kKeepWhitespace,
                           fxl::SplitResult::kSplitWantNonEmpty);
  path_elements.pop_back();

  if (!files::CreateDirectoryAt(sink_root_fd.get(), fxl::JoinStrings(path_elements, "/"))) {
    FX_LOGS(WARNING) << "Failed to create directory for " << relative_path << " under sink root";
    return false;
  }

  if (!files::WriteFileAt(sink_root_fd.get(), relative_path, content.data(), content.size())) {
    FX_LOGS(WARNING) << "Failed to write " << relative_path << " to sink root";
    return false;
  }

  return true;
}

bool GetNestedDirectories(const fbl::unique_fd& root_fd, std::vector<std::string>* directories) {
  if (!root_fd.is_valid()) {
    return false;
  }

  std::vector<std::string> found_directories;
  std::queue<std::string> to_search({"."});

  while (!to_search.empty()) {
    const std::string relative_path = to_search.front();
    to_search.pop();

    if (!files::IsDirectoryAt(root_fd.get(), relative_path)) {
      continue;
    }

    found_directories.push_back(relative_path);

    if (std::vector<std::string> contents;
        files::ReadDirContentsAt(root_fd.get(), relative_path, &contents)) {
      for (const auto& c : contents) {
        if (c != ".") {
          to_search.push(files::JoinPath(relative_path, c));
        }
      }
    }
  }

  directories->swap(found_directories);
  return true;
}

bool GetNestedFiles(const fbl::unique_fd& root_fd, std::vector<std::string>* files) {
  std::vector<std::string> dirs;
  if (!GetNestedDirectories(root_fd, &dirs)) {
    return false;
  }

  std::vector<std::string> found_files;
  for (const auto& dir : dirs) {
    std::vector<std::string> contents;
    if (!files::ReadDirContentsAt(root_fd.get(), dir, &contents)) {
      return false;
    }

    for (const auto& item : contents) {
      const std::string path = files::JoinPath(dir, item);
      if (files::IsFileAt(root_fd.get(), path)) {
        found_files.push_back(path);
      }
    }
  }

  files->swap(found_files);
  return true;
}

bool Migrate(const fbl::unique_fd& source_root_fd, const fbl::unique_fd& sink_root_fd) {
  if (!sink_root_fd.is_valid()) {
    return false;
  }

  if (!source_root_fd.is_valid()) {
    return true;
  }

  std::vector<std::string> relative_dirs;
  if (!GetNestedDirectories(source_root_fd, &relative_dirs)) {
    FX_LOGS(WARNING) << "Unable to get nested directories";
    return false;
  }

  relative_dirs.erase(std::remove(relative_dirs.begin(), relative_dirs.end(), "."),
                      relative_dirs.end());

  for (const auto& relative_dir : relative_dirs) {
    if (!files::CreateDirectoryAt(sink_root_fd.get(), relative_dir)) {
      FX_LOGS(WARNING) << "Unable to create directory " << relative_dir;
      return false;
    }
  }

  std::vector<std::string> relative_files;
  if (!GetNestedFiles(source_root_fd, &relative_files)) {
    FX_LOGS(WARNING) << "Unable to get nested files";
    return false;
  }

  for (const auto& relative_file : relative_files) {
    if (!CopyFile(source_root_fd, sink_root_fd, relative_file)) {
      FX_LOGS(WARNING) << "Unable to copy file " << relative_file;
      return false;
    }

    if (!files::DeletePathAt(source_root_fd.get(), relative_file, /*recursive=*/true)) {
      FX_LOGS(WARNING) << "Unable to delete file " << relative_file;
      return false;
    }
  }

  for (const auto& relative_dir : relative_dirs) {
    if (!files::DeletePathAt(source_root_fd.get(), relative_dir, /*recursive=*/true)) {
      FX_LOGS(WARNING) << "Unable to delete " << relative_dir << " from original root";
      return false;
    }
  }

  return true;
}

}  // namespace forensics::feedback

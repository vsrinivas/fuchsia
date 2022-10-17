// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/copier.h"

#include <dirent.h>
#include <fcntl.h>
#include <lib/syslog/cpp/macros.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zircon/errors.h>

#include <filesystem>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <fbl/unique_fd.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"

namespace fshost {
namespace {

struct DirCloser {
  void operator()(DIR* dir) { closedir(dir); }
};
// RAII wrapper around a DIR* that close the DIR when it goes out of scope.
using UniqueDir = std::unique_ptr<DIR, DirCloser>;

UniqueDir OpenDir(fbl::unique_fd fd) {
  UniqueDir dir(fdopendir(fd.get()));
  if (dir) {
    // DIR only takes ownership of the file descriptor on success.
    fd.release();
  }
  return dir;
}

bool IsPathExcluded(const std::vector<std::filesystem::path>& excluded_paths,
                    const std::filesystem::path& path) {
  for (const auto& exclusion : excluded_paths) {
    if (exclusion.empty()) {
      // Skip the empty path. It would cause all files to be excluded which is probably not what the
      // caller wanted.
      continue;
    }
    auto exclusion_it = exclusion.begin();
    auto path_it = path.begin();
    while (exclusion_it != exclusion.end() && path_it != path.end()) {
      if (*exclusion_it != *path_it) {
        break;
      }
      ++exclusion_it;
      ++path_it;
    }
    if (exclusion_it == exclusion.end()) {
      return true;
    }
  }
  return false;
}

Copier::DirectoryEntry* GetEntry(Copier::DirectoryEntries& entries, const std::string& name) {
  for (auto& entry : entries) {
    if (std::visit([&name](auto& entry) { return entry.name == name; }, entry)) {
      return &entry;
    }
  }
  return nullptr;
}

}  // namespace

zx::result<Copier> Copier::Read(fbl::unique_fd root_fd,
                                const std::vector<std::filesystem::path>& excluded_paths) {
  struct PendingRead {
    UniqueDir dir;
    DirectoryEntries* entries;
    // The path relative to |root_fd|.
    std::filesystem::path path;
  };
  std::vector<PendingRead> pending;

  Copier copier;
  {
    UniqueDir dir = OpenDir(std::move(root_fd));
    if (!dir)
      return zx::error(ZX_ERR_BAD_STATE);
    pending.push_back({
        .dir = std::move(dir),
        .entries = &copier.entries_,
        .path = "",
    });
  }
  while (!pending.empty()) {
    PendingRead current = std::move(pending.back());
    pending.pop_back();
    struct dirent* entry;
    while ((entry = readdir(current.dir.get())) != nullptr) {
      std::string name(entry->d_name);
      std::filesystem::path path = current.path / name;
      if (IsPathExcluded(excluded_paths, path))
        continue;
      fbl::unique_fd fd(openat(dirfd(current.dir.get()), name.c_str(), O_RDONLY));
      if (!fd)
        return zx::error(ZX_ERR_BAD_STATE);
      switch (entry->d_type) {
        case DT_REG: {
          struct stat stat_buf;
          if (fstat(fd.get(), &stat_buf) != 0) {
            return zx::error(ZX_ERR_BAD_STATE);
          }
          std::string buf;
          buf.reserve(stat_buf.st_size);
          if (!files::ReadFileDescriptorToString(fd.get(), &buf)) {
            return zx::error(ZX_ERR_BAD_STATE);
          }
          current.entries->push_back(File{std::move(name), std::move(buf)});
          break;
        }
        case DT_DIR: {
          if (name == "." || name == "..")
            continue;
          UniqueDir child_dir = OpenDir(std::move(fd));
          if (!child_dir)
            return zx::error(ZX_ERR_BAD_STATE);
          current.entries->push_back(Directory{std::move(name), {}});
          pending.push_back({
              .dir = std::move(child_dir),
              .entries = &std::get<Directory>(current.entries->back()).entries,
              .path = std::move(path),
          });
          break;
        }
      }
    }
  }
  return zx::ok(std::move(copier));
}

zx_status_t Copier::Write(fbl::unique_fd root_fd) const {
  std::vector<std::pair<fbl::unique_fd, const DirectoryEntries*>> pending;
  pending.emplace_back(root_fd.duplicate(), &entries_);
  while (!pending.empty()) {
    fbl::unique_fd fd = std::move(pending.back().first);
    const DirectoryEntries* entries = pending.back().second;
    pending.pop_back();
    // Fail to compile if extra types are added.
    static_assert(std::variant_size_v<DirectoryEntry> == 2);
    for (const auto& entry : *entries) {
      if (std::holds_alternative<File>(entry)) {
        const File& file = std::get<File>(entry);
        if (!files::WriteFileAt(fd.get(), file.name, file.contents.data(),
                                static_cast<ssize_t>(file.contents.size()))) {
          FX_LOGS(ERROR) << "Unable to write to " << file.name;
          return ZX_ERR_BAD_STATE;
        }
      } else if (std::holds_alternative<Directory>(entry)) {
        const Directory& directory = std::get<Directory>(entry);
        if (!files::CreateDirectoryAt(fd.get(), directory.name)) {
          FX_LOGS(ERROR) << "Unable to make directory " << directory.name;
          return ZX_ERR_BAD_STATE;
        }
        fbl::unique_fd child_fd(openat(fd.get(), directory.name.c_str(), O_RDONLY));
        if (!child_fd) {
          FX_LOGS(ERROR) << "Unable to open directory " << directory.name;
          return ZX_ERR_BAD_STATE;
        }
        pending.emplace_back(std::move(child_fd), &directory.entries);
      }
    }
  }
  if (syncfs(root_fd.get())) {
    FX_LOGS(ERROR) << "Failed to sync filesystem state: " << strerror(errno);
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

zx::result<> Copier::InsertFile(const std::filesystem::path& path, std::string contents) {
  if (path.filename().empty() || path.is_absolute()) {
    // |path| was either empty, ended with '/', or started with '/'.
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  DirectoryEntries* entries = &entries_;
  for (const auto& parent : path.parent_path()) {
    DirectoryEntry* entry = GetEntry(*entries, parent);
    if (entry == nullptr) {
      entries->push_back(Directory{parent, {}});
      entries = &std::get<Directory>(entries->back()).entries;
    } else if (Directory* child_dir = std::get_if<Directory>(entry); child_dir != nullptr) {
      entries = &child_dir->entries;
    } else {
      // A file exists where a directory needed to be created.
      return zx::error(ZX_ERR_BAD_STATE);
    }
  }
  if (GetEntry(*entries, path.filename()) != nullptr) {
    // The file already exists.
    return zx::error(ZX_ERR_ALREADY_EXISTS);
  }
  entries->push_back(File{path.filename(), std::move(contents)});
  return zx::ok();
}

}  // namespace fshost

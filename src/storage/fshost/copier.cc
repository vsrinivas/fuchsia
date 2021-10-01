// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/copier.h"

#include <dirent.h>
#include <lib/syslog/cpp/macros.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zircon/errors.h>

#include <filesystem>
#include <memory>
#include <string>
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

}  // namespace

zx::status<Copier> Copier::Read(fbl::unique_fd root_fd,
                                const std::vector<std::filesystem::path>& excluded_paths) {
  struct PendingRead {
    UniqueDir dir;
    Tree* tree;
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
        .tree = &copier.tree_,
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
          current.tree->tree.emplace_back(std::move(name), std::move(buf));
          break;
        }
        case DT_DIR: {
          if (name == "." || name == "..")
            continue;
          auto child_tree = std::make_unique<Tree>();
          UniqueDir child_dir = OpenDir(std::move(fd));
          if (!child_dir)
            return zx::error(ZX_ERR_BAD_STATE);
          pending.push_back({
              .dir = std::move(child_dir),
              .tree = child_tree.get(),
              .path = std::move(path),
          });
          current.tree->tree.emplace_back(std::move(name), std::move(child_tree));
          break;
        }
      }
    }
  }
  return zx::ok(std::move(copier));
}

zx_status_t Copier::Write(fbl::unique_fd root_fd) const {
  std::vector<std::pair<fbl::unique_fd, const Tree*>> pending;
  pending.emplace_back(std::move(root_fd), &tree_);
  while (!pending.empty()) {
    fbl::unique_fd fd = std::move(pending.back().first);
    const Tree* tree = pending.back().second;
    pending.pop_back();
    for (const auto& [name, child] : tree->tree) {
      auto child_data = std::get_if<0>(&child);
      if (child_data) {
        if (!files::WriteFileAt(fd.get(), name, child_data->data(),
                                static_cast<ssize_t>(child_data->size()))) {
          FX_LOGS(ERROR) << "Unable to write to " << name;
          return ZX_ERR_BAD_STATE;
        }
      } else {
        if (!files::CreateDirectoryAt(fd.get(), name)) {
          FX_LOGS(ERROR) << "Unable to make directory " << name;
          return ZX_ERR_BAD_STATE;
        }
        fbl::unique_fd child_fd(openat(fd.get(), name.c_str(), O_RDONLY));
        if (!child_fd) {
          FX_LOGS(ERROR) << "Unable to open directory " << name;
          return ZX_ERR_BAD_STATE;
        }
        pending.emplace_back(std::move(child_fd), std::get<1>(child).get());
      }
    }
  }
  return ZX_OK;
}

}  // namespace fshost

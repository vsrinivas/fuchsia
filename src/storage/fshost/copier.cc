// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/copier.h"

#include <dirent.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>

namespace fshost {

zx::status<Copier> Copier::Read(fbl::unique_fd root_fd) {
  std::vector<std::pair<DIR*, Tree*>> pending;
  DIR* current = nullptr;
  auto defer = fit::defer([&pending, &current] {
    for (auto [dir, tree] : pending)
      closedir(dir);
    if (current)
      closedir(current);
  });
  Copier copier;
  {
    DIR* dir = fdopendir(root_fd.release());
    if (!dir)
      return zx::error(ZX_ERR_BAD_STATE);
    pending.push_back(std::make_pair(dir, &copier.tree_));
  }
  while (!pending.empty()) {
    current = pending.back().first;
    Tree* tree = pending.back().second;
    pending.pop_back();
    struct dirent* entry;
    while ((entry = readdir(current)) != nullptr) {
      fbl::unique_fd fd(openat(dirfd(current), entry->d_name, O_RDONLY));
      if (!fd)
        return zx::error(ZX_ERR_BAD_STATE);
      switch (entry->d_type) {
        case DT_REG: {
          struct stat stat_buf;
          if (fstat(fd.get(), &stat_buf) != 0) {
            return zx::error(ZX_ERR_BAD_STATE);
          }
          std::vector<uint8_t> buf(stat_buf.st_size);
          if (read(fd.get(), buf.data(), stat_buf.st_size) != stat_buf.st_size) {
            return zx::error(ZX_ERR_BAD_STATE);
          }
          tree->tree.push_back(std::make_pair(std::string(entry->d_name), std::move(buf)));
          break;
        }
        case DT_DIR: {
          if (!strcmp(entry->d_name, "."))
            continue;
          auto child_tree = std::make_unique<Tree>();
          DIR* child_dir = fdopendir(fd.release());
          if (!child_dir)
            return zx::error(ZX_ERR_BAD_STATE);
          pending.push_back(std::make_pair(child_dir, child_tree.get()));
          tree->tree.push_back(std::make_pair(std::string(entry->d_name), std::move(child_tree)));
          break;
        }
      }
    }
    closedir(current);
    current = nullptr;
  }
  return zx::ok(std::move(copier));
}

zx_status_t Copier::Write(fbl::unique_fd root_fd) const {
  std::vector<std::pair<fbl::unique_fd, const Tree*>> pending;
  pending.push_back(std::make_pair(std::move(root_fd), &tree_));
  while (!pending.empty()) {
    fbl::unique_fd fd = std::move(pending.back().first);
    const Tree* tree = pending.back().second;
    pending.pop_back();
    for (const auto& [name, child] : tree->tree) {
      auto child_data = std::get_if<0>(&child);
      if (child_data) {
        fbl::unique_fd child_fd(openat(fd.get(), name.c_str(), O_RDWR | O_CREAT, 0666));
        if (!child_fd) {
          FX_LOGS(ERROR) << "Unable to open " << name;
          return ZX_ERR_BAD_STATE;
        }
        if (write(child_fd.get(), child_data->data(), child_data->size()) !=
            static_cast<ssize_t>(child_data->size())) {
          FX_LOGS(ERROR) << "Unable to write to " << name;
          return ZX_ERR_BAD_STATE;
        }
      } else {
        if (mkdirat(fd.get(), name.c_str(), 0777)) {
          FX_LOGS(ERROR) << "Unable to make directory " << name;
          return ZX_ERR_BAD_STATE;
        }
        fbl::unique_fd child_fd(openat(fd.get(), name.c_str(), O_RDONLY));
        if (!child_fd) {
          FX_LOGS(ERROR) << "Unable to open directory " << name;
          return ZX_ERR_BAD_STATE;
        }
        pending.push_back(std::make_pair(std::move(child_fd), std::get<1>(child).get()));
      }
    }
  }
  return ZX_OK;
}

}  // namespace fshost


// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/storage_metrics.h"

#include <dirent.h>
#include <fcntl.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <sys/stat.h>
#include <threads.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <mutex>
#include <unordered_map>

#include <fbl/unique_fd.h>
#include <src/lib/files/directory.h>
#include <src/lib/files/path.h>

namespace {

constexpr uint32_t kRecursionLimit = 64;

// Called with the fd for a component directory. Recursively iterates through the structure
// returning the total bytes and inodes usage.
StorageMetrics::Usage SumDirUsage(fbl::unique_fd unique_fd, uint32_t recursion = 0) {
  if (recursion >= kRecursionLimit) {
    return {0, 0};
  }

  DIR* dir_stream = fdopendir(unique_fd.get());
  int fd = -1;
  if (dir_stream) {
    // This is now handled by closedir()
    fd = unique_fd.release();
  } else {
    FX_LOGS(WARNING) << "Failed to read dir listing. Skipping.";
    return {0, 0};
  }
  auto close_dir = fit::defer([dir_stream]() { closedir(dir_stream); });

  struct dirent* ent = nullptr;
  size_t total_bytes = 0;
  size_t total_files = 0;
  while ((ent = readdir(dir_stream))) {
    if (strncmp(ent->d_name, ".", 1) == 0) {
      // Don't recurse on '.', do nothing.
    } else if (ent->d_type == DT_LNK) {
      // TODO(fxbug.dev/69017): Handle symlink sizes properly since they can consume blocks
      // depending on fs, but currently none of our filesystems support this.
      total_files++;
    } else {
      fbl::unique_fd child(openat(fd, ent->d_name, O_RDONLY));
      if (!child) {
        FX_LOGS(WARNING) << "Failed to open subdir: " << ent->d_name;
        continue;
      }
      struct stat st;
      if (fstat(child.get(), &st) != 0) {
        FX_LOGS(WARNING) << "Failed to stat file: " << ent->d_name;
      } else {
        total_bytes += st.st_blocks * st.st_blksize;
        total_files++;
      }
      if (ent->d_type == DT_DIR) {
        auto bytes_files = SumDirUsage(std::move(child), recursion + 1);
        total_bytes += bytes_files.bytes;
        total_files += bytes_files.inodes;
      }
    }
  }
  return {total_bytes, total_files};
}

// Alternate recursion, so forward declaring.
void SumComponentsForPath(fbl::unique_fd unique_fd, StorageMetrics::UsageMap* usage);

// Takes an fd for a realm directory and enters each realm to sum them as a top level component
// storage path.
void SumRealmForPath(fbl::unique_fd unique_fd, StorageMetrics::UsageMap* usage) {
  DIR* dir_stream = fdopendir(unique_fd.get());
  int fd = -1;
  if (dir_stream) {
    // This is now handled by closedir()
    fd = unique_fd.release();
  } else {
    FX_LOGS(WARNING) << "Failed to read watched dir listing. Skipping.";
    return;
  }
  auto close_dir = fit::defer([dir_stream]() { closedir(dir_stream); });

  struct dirent* ent = nullptr;
  while ((ent = readdir(dir_stream))) {
    if (strncmp(ent->d_name, ".", 1) == 0) {
      // Don't treat `.` as a component directory, do nothing.
    } else {
      fbl::unique_fd child(openat(fd, ent->d_name, O_DIRECTORY | O_RDONLY));
      if (!child) {
        FX_LOGS(WARNING) << "Failed to open subdir: " << ent->d_name;
        continue;
      }
      SumComponentsForPath(std::move(child), usage);
    }
  }
}

// Given the fd for a top level component storage directory it will add all usage to the usage
// map keyed on the top level directory name inside it.
void SumComponentsForPath(fbl::unique_fd unique_fd, StorageMetrics::UsageMap* usage) {
  DIR* dir_stream = fdopendir(unique_fd.get());
  int fd = -1;
  if (dir_stream) {
    // This is now handled by closedir()
    fd = unique_fd.release();
  } else {
    FX_LOGS(WARNING) << "Failed to read watched dir listing. Skipping.";
    return;
  }
  auto close_dir = fit::defer([dir_stream]() { closedir(dir_stream); });

  struct dirent* ent = nullptr;
  while ((ent = readdir(dir_stream))) {
    if (strncmp(ent->d_name, ".", 1) == 0) {
      // Don't treat `.` as a component directory, do nothing.
    } else if (strncmp(ent->d_name, "r", 1) == 0) {
      // Entering a realm, recurse.
      fbl::unique_fd child(openat(fd, ent->d_name, O_DIRECTORY | O_RDONLY));
      if (!child) {
        FX_LOGS(WARNING) << "Failed to open subdir: " << ent->d_name;
        continue;
      }
      SumRealmForPath(std::move(child), usage);
    } else {
      fbl::unique_fd child(openat(fd, ent->d_name, O_DIRECTORY | O_RDONLY));
      if (!child) {
        FX_LOGS(WARNING) << "Failed to open subdir: " << ent->d_name;
        continue;
      }
      usage->AddForKey(ent->d_name, SumDirUsage(std::move(child)));
    }
  }
}

}  // namespace

void StorageMetrics::UsageMap::AddForKey(const std::string& name, const Usage& usage) {
  auto node = map_.find(name);
  if (node == map_.end()) {
    map_[name] = usage;
  } else {
    Usage& old = node->second;
    map_[name] = {old.bytes + usage.bytes, old.inodes + usage.inodes};
  }
}

fpromise::promise<inspect::Inspector> StorageMetrics::InspectByteUsage(
    const std::string& path) const {
  inspect::Inspector inspector;
  std::lock_guard<std::mutex> lock(usage_lock_);
  auto entry = usage_.find(path);
  if (entry == usage_.end()) {
    // No data populated yet for this path.
    return fpromise::make_ok_promise(inspector);
  }
  for (const auto& it : entry->second.map()) {
    inspector.GetRoot().CreateUint(it.first, it.second.bytes, &inspector);
  }
  return fpromise::make_ok_promise(inspector);
}

fpromise::promise<inspect::Inspector> StorageMetrics::InspectInodeUsage(
    const std::string& path) const {
  inspect::Inspector inspector;
  std::lock_guard<std::mutex> lock(usage_lock_);
  auto entry = usage_.find(path);
  if (entry == usage_.end()) {
    // No data populated yet for this path.
    return fpromise::make_ok_promise(inspector);
  }
  for (const auto& it : entry->second.map()) {
    inspector.GetRoot().CreateUint(it.first, it.second.inodes, &inspector);
  }
  return fpromise::make_ok_promise(inspector);
}

StorageMetrics::StorageMetrics(std::vector<std::string> paths_to_watch, inspect::Node inspect_node)
    : paths_to_watch_(std::move(paths_to_watch)),
      inspect_root_(std::move(inspect_node)),
      inspect_bytes_stats_(inspect_root_.CreateChild("bytes")),
      inspect_inode_stats_(inspect_root_.CreateChild("inodes")) {
  for (auto& it : paths_to_watch_) {
    lazy_nodes_.push_back(inspect_bytes_stats_.CreateLazyNode(
        it, [this, &it] { return this->InspectByteUsage(it); }));
    lazy_nodes_.push_back(inspect_inode_stats_.CreateLazyNode(
        it, [this, &it] { return this->InspectInodeUsage(it); }));
  }
}

std::unordered_map<std::string, StorageMetrics::UsageMap> StorageMetrics::GatherStorageUsage()
    const {
  std::unordered_map<std::string, StorageMetrics::UsageMap> usage_by_path;
  for (const std::string& dir : paths_to_watch_) {
    UsageMap usage;
    fbl::unique_fd fd(open(dir.c_str(), O_DIRECTORY | O_RDONLY));
    if (!fd) {
      FX_LOGS(WARNING) << "Failed to open path: " << dir;
      continue;
    }
    SumComponentsForPath(std::move(fd), &usage);
    usage_by_path[dir] = std::move(usage);
  }
  return usage_by_path;
}

void StorageMetrics::PollStorage() {
  auto new_usage = GatherStorageUsage();
  {
    std::lock_guard<std::mutex> lock(usage_lock_);
    usage_ = std::move(new_usage);
  }

  async::PostDelayedTask(
      loop_.dispatcher(), [this] { this->PollStorage(); }, kPollCycle);
}

zx::result<> StorageMetrics::Run() {
  zx_status_t status = loop_.StartThread("StorageMetrics");
  if (status != ZX_OK) {
    return zx::error(status);
  }
  status = async::PostTask(loop_.dispatcher(), [this] { this->PollStorage(); });
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok();
}

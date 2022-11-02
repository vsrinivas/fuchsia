// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/storage_watchdog.h"

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <sys/types.h>

#include <string>

#include <re2/re2.h>
#include <src/lib/files/directory.h>
#include <src/lib/files/path.h>

#include "src/lib/fxl/strings/concatenate.h"

namespace {

namespace fio = fuchsia_io;

// Delete the given dirent inside the openend directory. If the dirent is a
// directory itself, it will be recursively deleted.
void DeleteDirentInFd(int dir_fd, struct dirent* ent) {
  if (ent->d_type == DT_DIR) {
    int child_dir = openat(dir_fd, ent->d_name, O_DIRECTORY);
    if (child_dir == -1) {
      return;
    }
    DIR* child_dir_stream = fdopendir(child_dir);
    struct dirent* child_ent;
    while ((child_ent = readdir(child_dir_stream))) {
      if (strncmp(child_ent->d_name, ".", 2) != 0) {
        DeleteDirentInFd(child_dir, child_ent);
      }
    }
    closedir(child_dir_stream);
    unlinkat(dir_fd, ent->d_name, AT_REMOVEDIR);
  } else {
    unlinkat(dir_fd, ent->d_name, 0);
  }
}

// PurgeCacheIn will remove elements in cache directories inside dir_fd,
// recurse on any nested realms in dir_fd, and close dir_fd when its done.
void PurgeCacheIn(int dir_fd) {
  static const re2::RE2* const kV1StorageDirRegex = new re2::RE2("[^:#]*:[^:#]*:[^:#]*#[^#]*");
  static const re2::RE2* const kV2StorageDirRegex = new re2::RE2("(data)|([0-9a-f]{64})");

  DIR* dir_stream = fdopendir(dir_fd);
  // For all children in the path we're looking at, if it's named "r", then
  // it's a child realm that we should walk into. If it's not, it's a
  // component's cache that should be cleaned. Note that the path naming logic
  // implemented in realm.cc:IsolatedPathForPackage() makes it impossible for
  // a component to be named "r".
  struct dirent* ent = nullptr;
  while ((ent = readdir(dir_stream))) {
    if (std::string(ent->d_name) == ".") {
      // Don't treat `.` as a component directory to be deleted!
      continue;
    } else if (re2::RE2::FullMatch(std::string(ent->d_name), *kV1StorageDirRegex) ||
               re2::RE2::FullMatch(std::string(ent->d_name), *kV2StorageDirRegex)) {
      int component_dir = openat(dir_fd, ent->d_name, O_DIRECTORY);
      if (component_dir == -1) {
        continue;
      }
      DIR* component_dir_stream = fdopendir(component_dir);
      struct dirent* ent = nullptr;
      while ((ent = readdir(component_dir_stream))) {
        if (std::string(ent->d_name) != ".") {
          DeleteDirentInFd(component_dir, ent);
        }
      }
      closedir(component_dir_stream);
    } else {
      // This is a container directory such as "r", "children", or <v2_moniker>. Open it and
      // recurse.
      int r_dir = openat(dir_fd, ent->d_name, O_DIRECTORY);
      if (r_dir == -1) {
        // We failed to open the directory. Keep going, as we want to delete as
        // much as we can!
        continue;
      }
      PurgeCacheIn(r_dir);
    }
  }
  closedir(dir_stream);
}
}  // namespace

// GetStorageUsage will return the percentage, from 0 to 100, of used bytes on
// the disk located at this.path_to_watch_.
StorageWatchdog::StorageUsage StorageWatchdog::GetStorageUsage() {
  TRACE_DURATION("appmgr", "StorageWatchdog::GetStorageUsage");
  fbl::unique_fd fd;
  fd.reset(open(path_to_watch_.c_str(), O_RDONLY));
  if (!fd) {
    FX_LOGS(WARNING) << "storage_watchdog: could not open target: " << path_to_watch_;
    return StorageUsage();
  }

  fuchsia_io::wire::FilesystemInfo info;
  fdio_cpp::FdioCaller caller(std::move(fd));
  zx_status_t status = GetFilesystemInfo(caller.borrow_channel(), &info);
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "storage_watchdog: cannot query filesystem: " << status;
    return StorageUsage();
  }
  info.name[fuchsia_io::wire::kMaxFsNameBuffer - 1] = '\0';

  // The number of bytes which may be allocated plus the number of bytes which have been allocated.
  // |total_bytes| is the amount of data (not counting metadata like inode storage) that minfs has
  // currently allocated from the volume manager, while used_bytes is the amount of those actually
  // used for current storage.
  size_t total = info.free_shared_pool_bytes + info.total_bytes;

  if (total == 0) {
    FX_LOGS(WARNING) << "storage_watchdog: unable to determine storage "
                     << "pressure";
    return StorageUsage();
  } else if (total < info.used_bytes) {
    FX_LOGS(WARNING) << "storage_watchdog: Usage (" << info.used_bytes
                     << ") exceeds reported total (" << total << ")";
  }

  return StorageUsage{
      .avail = total,
      .used = info.used_bytes,
  };
}

void StorageWatchdog::CheckStorage(async_dispatcher_t* dispatcher, size_t threshold_purge_percent) {
  StorageUsage usage = this->GetStorageUsage();
  this->bytes_used_.Set(usage.used);
  this->bytes_avail_.Set(usage.avail);
  if (usage.percent() >= threshold_purge_percent) {
    FX_LOGS(INFO) << "storage usage has reached threshold of " << threshold_purge_percent
                  << "\%, purging the cache now";
    this->PurgeCache();

    StorageUsage usage_after = this->GetStorageUsage();
    FX_LOGS(INFO) << "cache purge is complete, new storage usage is at " << usage.percent()
                  << "\% capacity (" << usage.used << " used, " << usage.avail << " avail)";
    if (usage_after.percent() >= threshold_purge_percent) {
      FX_LOGS(WARNING) << "usage still exceeds threshold after purge (" << usage.used << " used, "
                       << usage.avail << " avail)";
    }
  }

  async::PostDelayedTask(
      dispatcher, [this, dispatcher] { this->CheckStorage(dispatcher); }, zx::sec(60));
}

void StorageWatchdog::Run(async_dispatcher_t* dispatcher) {
  async::PostTask(dispatcher, [this, dispatcher] { this->CheckStorage(dispatcher); });
}

// PurgeCache will remove cache items from this.path_to_clean_.
void StorageWatchdog::PurgeCache() {
  TRACE_DURATION("appmgr", "StorageWatchdog::PurgeCache");
  // Walk the directory tree from `path_to_clean_`.
  int dir_fd = open(path_to_clean_.c_str(), O_DIRECTORY);
  if (dir_fd == -1) {
    if (errno == ENOENT) {
      FX_LOGS(INFO) << "nothing in cache to purge";
    } else {
      FX_LOGS(ERROR) << "error opening directory: " << errno;
    }
    return;
  }
  PurgeCacheIn(dir_fd);
}

zx_status_t StorageWatchdog::GetFilesystemInfo(zx_handle_t directory,
                                               fuchsia_io::wire::FilesystemInfo* out_info) {
  auto result =
      fidl::WireCall(fidl::UnownedClientEnd<fuchsia_io::Directory>(directory))->QueryFilesystem();
  if (result.ok())
    *out_info = *result.value().info;
  return !result.ok() ? result.status() : result.value().s;
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/storage_watchdog.h"

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/cpp/caller.h>
#include <sys/types.h>

#include <src/lib/files/directory.h>
#include <src/lib/files/path.h>
#include <trace/event.h>

#include "src/lib/fxl/strings/concatenate.h"

namespace {
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
  DIR* dir_stream = fdopendir(dir_fd);
  // For all children in the path we're looking at, if it's named "r", then
  // it's a child realm that we should walk into. If it's not, it's a
  // component's cache that should be cleaned. Note that the path naming logic
  // implemented in realm.cc:IsolatedPathForPackage() makes it impossible for
  // a component to be named "r".
  struct dirent* ent = nullptr;
  while ((ent = readdir(dir_stream))) {
    if (strncmp(ent->d_name, ".", 2) == 0) {
      // Don't treat `.` as a component directory to be deleted!
      continue;
    } else if (strncmp(ent->d_name, "r", 2) == 0) {
      // This is a realm, open and queue up the child realms to be cleaned
      int r_dir = openat(dir_fd, ent->d_name, O_DIRECTORY);
      if (r_dir == -1) {
        // We failed to open the directory. Keep going, as we want to delete as
        // much as we can!
        continue;
      }
      DIR* r_dir_stream = fdopendir(r_dir);
      struct dirent* ent = nullptr;
      while ((ent = readdir(r_dir_stream))) {
        if (strncmp(ent->d_name, ".", 2) != 0) {
          int new_dir_fd = openat(r_dir, ent->d_name, O_DIRECTORY);
          if (new_dir_fd == -1) {
            continue;
          }
          PurgeCacheIn(new_dir_fd);
        }
      }
      closedir(r_dir_stream);
    } else {
      int component_dir = openat(dir_fd, ent->d_name, O_DIRECTORY);
      if (component_dir == -1) {
        continue;
      }
      DIR* component_dir_stream = fdopendir(component_dir);
      struct dirent* ent = nullptr;
      while ((ent = readdir(component_dir_stream))) {
        if (strncmp(ent->d_name, ".", 2) != 0) {
          DeleteDirentInFd(component_dir, ent);
        }
      }
      closedir(component_dir_stream);
    }
  }
  closedir(dir_stream);
}
}  // namespace

// GetStorageUsage will return the percentage, from 0 to 100, of used bytes on
// the disk located at this.path_to_watch_.
size_t StorageWatchdog::GetStorageUsage() {
  TRACE_DURATION("appmgr", "StorageWatchdog::GetStorageUsage");
  fbl::unique_fd fd;
  fd.reset(open(path_to_watch_.c_str(), O_RDONLY));
  if (!fd) {
    FXL_LOG(WARNING) << "storage_watchdog: could not open target: " << path_to_watch_;
    return 0;
  }

  fuchsia_io_FilesystemInfo info;
  zx_status_t status;
  fdio_cpp::FdioCaller caller(std::move(fd));
  zx_status_t io_status =
      fuchsia_io_DirectoryAdminQueryFilesystem(caller.borrow_channel(), &status, &info);
  if (io_status != ZX_OK || status != ZX_OK) {
    FXL_LOG(WARNING) << "storage_watchdog: cannot query filesystem: " << io_status << " OR "
                     << status;
    return 0;
  }
  info.name[fuchsia_io_MAX_FS_NAME_BUFFER - 1] = '\0';

  // The number of bytes which may be allocated plus the number of bytes which
  // have been allocated
  size_t free_plus_allocated = info.free_shared_pool_bytes + info.total_bytes;

  if (free_plus_allocated == 0) {
    FXL_LOG(WARNING) << "storage_watchdog: unable to determine storage "
                     << "pressure";
    return 0;
  }

  // The number of used bytes (*100, because we want a percent) over the number
  // of bytes which may be used
  size_t use_percentage = info.used_bytes * 100 / free_plus_allocated;

  return use_percentage;
}

void StorageWatchdog::CheckStorage(async_dispatcher_t* dispatcher) {
  size_t use_percentage = this->GetStorageUsage();

  if (use_percentage >= 95) {
    FXL_LOG(INFO) << "storage usage has reached " << use_percentage
                  << "%% capacity, purging the cache now";
    this->PurgeCache();
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
      FXL_LOG(INFO) << "nothing in cache to purge";
    } else {
      FXL_LOG(ERROR) << "error opening directory: " << errno;
    }
    return;
  }
  PurgeCacheIn(dir_fd);
  size_t use_percentage = this->GetStorageUsage();
  FXL_LOG(INFO) << "cache purge is complete, new storage usage is at " << use_percentage
                << "%% capacity";
}

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block-watcher.h"

#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/partition/c/fidl.h>
#include <inttypes.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fzl/time.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/device/block.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <utility>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/string_piece.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <gpt/gpt.h>
#include <minfs/minfs.h>
#include <zxcrypt/fdio-volume.h>

#include "block-device.h"
#include "fs-manager.h"
#include "pkgfs-launcher.h"
#include "zircon/errors.h"

namespace devmgr {
namespace {

constexpr char kPathBlockDeviceRoot[] = "/dev/class/block";

// Class used to pause/resume the block watcher.
class BlockWatcherStatus {
 public:
  // Get the BlockWatcherStatus instance.
  static BlockWatcherStatus* Get() {
    std::call_once(init_flag, []() { singleton = new BlockWatcherStatus(); });
    return singleton;
  }

  // Increment the pause count for the block watcher.
  // This function will not return until the block watcher
  // is no longer running.
  // The block watcher will drop all new device events while paused.
  zx_status_t Pause() {
    auto guard = std::lock_guard<std::mutex>{lock_};
    if (pause_count_ == std::numeric_limits<unsigned int>::max()) {
      return ZX_ERR_UNAVAILABLE;
    }
    pause_count_++;
    return ZX_OK;
  }

  // Decrement the pause count for the block watcher.
  zx_status_t Resume() {
    auto guard = std::lock_guard<std::mutex>{lock_};
    if (pause_count_ == 0) {
      return ZX_ERR_BAD_STATE;
    }
    pause_count_--;
    return ZX_OK;
  }

  // Acquire the lock. Used by |BlockDeviceCallback| to indicate
  // it is doing work.
  void Lock() TA_ACQ(lock_) { lock_.lock(); }

  // Release the lock.
  void Unlock() TA_REL(lock_) { lock_.unlock(); }

  // Returns true if the block watcher is paused.
  bool Paused() TA_REQ(lock_) { return pause_count_ > 0; }

 private:
  static std::once_flag init_flag;
  static BlockWatcherStatus* singleton;
  std::mutex lock_;
  unsigned int pause_count_ TA_GUARDED(lock_);
};

std::once_flag BlockWatcherStatus::init_flag;
BlockWatcherStatus* BlockWatcherStatus::singleton;

zx_status_t BlockDeviceCallback(int dirfd, int event, const char* name, void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }

  // Lock the block watcher, so any pause operations wait until after we're done.
  auto watcher = BlockWatcherStatus::Get();
  watcher->Lock();
  if (watcher->Paused()) {
    watcher->Unlock();
    return ZX_OK;
  }

  fbl::unique_fd device_fd(openat(dirfd, name, O_RDWR));
  if (!device_fd) {
    watcher->Unlock();
    return ZX_OK;
  }

  auto mounter = static_cast<FilesystemMounter*>(cookie);
  BlockDevice device(mounter, std::move(device_fd));
  zx_status_t rc = device.Add();
  if (rc != ZX_OK) {
    // This callback has to return ZX_OK for resiliency reasons, or we'll
    // stop getting subsequent callbacks, but we should log loudly that we
    // tried to do something and that failed.
    fprintf(stderr, "fshost: (%s/%s) failed: %s\n", kPathBlockDeviceRoot, name,
            zx_status_get_string(rc));
  }
  watcher->Unlock();
  return ZX_OK;
}

}  // namespace

void BlockDeviceWatcher(std::unique_ptr<FsManager> fshost, BlockWatcherOptions options) {
  FilesystemMounter mounter(std::move(fshost), options);

  fbl::unique_fd dirfd(open(kPathBlockDeviceRoot, O_DIRECTORY | O_RDONLY));
  if (dirfd) {
    fdio_watch_directory(dirfd.get(), BlockDeviceCallback, ZX_TIME_INFINITE, &mounter);
  }
}

fbl::RefPtr<fs::Service> BlockWatcherServer::Create(devmgr::FsManager* fs_manager,
                                                    async_dispatcher* dispatcher) {
  return fbl::MakeRefCounted<fs::Service>([dispatcher, fs_manager](zx::channel chan) mutable {
    zx::event event;
    zx_status_t status = fs_manager->event()->duplicate(ZX_RIGHT_SAME_RIGHTS, &event);
    if (status != ZX_OK) {
      fprintf(stderr, "fshost: failed to duplicate event handle for admin service: %s\n",
              zx_status_get_string(status));
      return status;
    }

    status = fidl::BindSingleInFlightOnly(dispatcher, std::move(chan),
                                          std::make_unique<BlockWatcherServer>());
    if (status != ZX_OK) {
      fprintf(stderr, "fshost: failed to bind admin service: %s\n", zx_status_get_string(status));
      return status;
    }
    return ZX_OK;
  });
}

void BlockWatcherServer::Pause(PauseCompleter::Sync completer) {
  auto status = BlockWatcherStatus::Get()->Pause();
  completer.Reply(status);
}

void BlockWatcherServer::Resume(ResumeCompleter::Sync completer) {
  auto status = BlockWatcherStatus::Get()->Resume();
  completer.Reply(status);
}
}  // namespace devmgr

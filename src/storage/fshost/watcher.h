// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_WATCHER_H_
#define SRC_STORAGE_FSHOST_WATCHER_H_

#include <lib/fdio/cpp/caller.h>
#include <lib/stdcompat/span.h>
#include <zircon/status.h>

#include <fbl/unique_fd.h>

#include "src/storage/fshost/block-device-manager.h"
#include "src/storage/fshost/filesystem-mounter.h"

namespace fshost {

enum WatcherType {
  kWatcherTypeBlock = 0,
  kWatcherTypeNand,
  kWatcherTypeMax,
};

inline constexpr const char* kWatcherPaths[] = {
    "/dev/class/block",
    "/dev/class/nand",
};

static_assert(countof(kWatcherPaths) == WatcherType::kWatcherTypeMax,
              "Each watcher type must have a path");

class Watcher {
 public:
  zx_status_t ReinitWatcher();

  static std::vector<Watcher> CreateWatchers();

  using WatcherCallback = fit::function<bool(Watcher&, int, int, const char*)>;
  using AddDeviceCallback =
      fit::function<zx_status_t(BlockDeviceManager&, FilesystemMounter*, fbl::unique_fd)>;

  // Parse watch events from |buf|, calling |callback| for each event.
  // |callback| should return true if it receives an idle event and the block watcher is paused.
  void ProcessWatchMessages(cpp20::span<uint8_t> buf, WatcherCallback callback);
  zx_status_t AddDevice(BlockDeviceManager& manager, FilesystemMounter* mounter, fbl::unique_fd fd);
  WatcherType type() const { return type_; }
  bool ignore_existing() const { return ignore_existing_; }
  zx::unowned_channel borrow_watcher() { return watcher_.borrow(); }

 private:
  Watcher(WatcherType type, fdio_cpp::FdioCaller caller, AddDeviceCallback callback)
      : type_(type), caller_(std::move(caller)), add_device_(std::move(callback)) {}
  WatcherType type_;
  fdio_cpp::FdioCaller caller_;
  AddDeviceCallback add_device_;
  zx::channel watcher_;
  bool ignore_existing_ = false;
};

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_WATCHER_H_

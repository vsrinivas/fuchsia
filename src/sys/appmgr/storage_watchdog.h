// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_STORAGE_WATCHDOG_H_
#define SRC_SYS_APPMGR_STORAGE_WATCHDOG_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async/dispatcher.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/zx/channel.h>

#include <string>

const inspect::StringReference kBytesUsed("byte_used");
const inspect::StringReference kBytesAvailable("byte_available");

// StorageWatchdog can be used to observe the storage usage on a given
// partition, and when the storage usage reaches 95% the isolated component
// storage under a given path will be deleted.
class StorageWatchdog {
 public:
  StorageWatchdog(inspect::Node node, std::string path_to_watch, std::string path_to_clean)
      : node_(std::move(node)),
        bytes_used_(node_.CreateUint(kBytesUsed, 0)),
        bytes_avail_(node_.CreateUint(kBytesAvailable, 0)),
        path_to_watch_(std::move(path_to_watch)),
        path_to_clean_(std::move(path_to_clean)) {}

  void Run(async_dispatcher_t* dispatcher);
  struct StorageUsage {
    size_t avail = 0;
    size_t used = 0;
    size_t percent() const {
      if (avail > 0) {
        return used * 100 / avail;
      }
      return 0;
    }
  };
  StorageUsage GetStorageUsage();
  void CheckStorage(async_dispatcher_t* dispatcher,
                    size_t threshold_purge_percent = kCachePurgeThresholdPct);
  void PurgeCache();

  static constexpr size_t kCachePurgeThresholdPct = 95;

 protected:
  virtual zx_status_t GetFilesystemInfo(zx_handle_t directory,
                                        fuchsia_io::wire::FilesystemInfo* out_info);

 private:
  inspect::Node node_;
  inspect::UintProperty bytes_used_;
  inspect::UintProperty bytes_avail_;
  const std::string path_to_watch_;
  const std::string path_to_clean_;
};

#endif  // SRC_SYS_APPMGR_STORAGE_WATCHDOG_H_

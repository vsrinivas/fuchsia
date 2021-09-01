// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_STORAGE_WATCHDOG_H_
#define SRC_SYS_APPMGR_STORAGE_WATCHDOG_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/channel.h>

#include <string>

// StorageWatchdog can be used to observe the storage usage on a given
// partition, and when the storage usage reaches 95% the isolated component
// storage under a given path will be deleted.
class StorageWatchdog {
 public:
  StorageWatchdog(std::string path_to_watch, std::string path_to_clean)
      : path_to_watch_(path_to_watch), path_to_clean_(path_to_clean) {}

  void Run(async_dispatcher_t* dispatcher);
  size_t GetStorageUsage();
  void CheckStorage(async_dispatcher_t* dispatcher);
  void PurgeCache();

 protected:
  virtual zx_status_t GetFilesystemInfo(zx_handle_t directory,
                                        fuchsia_io::wire::FilesystemInfo* out_info);

 private:
  const std::string path_to_watch_;
  const std::string path_to_clean_;
};

#endif  // SRC_SYS_APPMGR_STORAGE_WATCHDOG_H_

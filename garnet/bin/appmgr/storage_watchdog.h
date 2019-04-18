// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_STORAGE_WATCHDOG_H_
#define GARNET_BIN_APPMGR_STORAGE_WATCHDOG_H_

#include <lib/async/dispatcher.h>

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

 private:
  std::string path_to_watch_;
  std::string path_to_clean_;
};

#endif  // GARNET_BIN_APPMGR_STORAGE_WATCHDOG_H_

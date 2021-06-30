// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_LOADER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_LOADER_H_

#include <threads.h>

#include <fbl/intrusive_double_list.h>

#include "driver.h"
#include "src/devices/bin/driver_manager/base_package_resolver.h"

class Coordinator;

class DriverLoader {
 public:
  // Takes in an unowned connection to boot arguments. boot_args must outlive DriverLoader.
  explicit DriverLoader(fidl::WireSyncClient<fuchsia_boot::Arguments>* boot_args)
      : base_resolver_(boot_args) {}
  ~DriverLoader();

  // Start a Thread to service loading drivers.
  // DriverLoader will join this thread when it destructs.
  // `coordinator` is a weak pointer and must be kept alive for the lifetime of DriverLoader.
  // `coordinator` is not thread safe, so any calls to it must be made on the
  // `coordinator->dispatcher()` thread.
  void StartLoadingThread(Coordinator* coordinator);

  internal::BasePackageResolver& base_resolver() { return base_resolver_; }

 private:
  // Search through the filesystem for drivers, load the drivers, then pass them to Coordinator
  // so they can be found. This needs to be called from its own thread because I/O operations are
  // blocking. `coordinator` is not thread safe so any calls to it must be made on the
  // `coordinator->dispatcher()` thread.
  void LoadDrivers();

  void DriverAdded(Driver* drv, const char* version);

  internal::BasePackageResolver base_resolver_;
  std::optional<thrd_t> loading_thread_;
  Coordinator* coordinator_ = nullptr;
  fbl::DoublyLinkedList<std::unique_ptr<Driver>> drivers_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_LOADER_H_

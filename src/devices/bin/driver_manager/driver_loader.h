// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_LOADER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_LOADER_H_

#include <threads.h>

#include <fbl/intrusive_double_list.h>

#include "src/devices/bin/driver_manager/base_package_resolver.h"
#include "src/devices/bin/driver_manager/device.h"
#include "src/devices/bin/driver_manager/driver.h"

class Coordinator;

class DriverLoader {
 public:
  // Takes in an unowned connection to boot arguments. boot_args must outlive DriverLoader.
  // Takes in an unowned connection to Coordinator. Coordinator must outlive DriverLoader.
  explicit DriverLoader(fidl::WireSyncClient<fuchsia_boot::Arguments>* boot_args,
                        Coordinator* coordinator)
      : base_resolver_(boot_args), coordinator_(coordinator) {}

  ~DriverLoader();

  // Start a Thread to service loading drivers.
  // DriverLoader will join this thread when it destructs.
  // `coordinator_` is not thread safe, so any calls to it must be made on the
  // `coordinator_->dispatcher()` thread.
  void StartSystemLoadingThread();

  std::vector<const Driver*> MatchDeviceDriverIndex(const fbl::RefPtr<Device>& dev,
                                                    std::string_view libname = "");
  const Driver* LibnameToDriver(std::string_view libname) const;

 private:
  bool MatchesLibnameDriverIndex(const std::string& driver_url, std::string_view libname);

  const Driver* LoadDriverUrlDriverIndex(const std::string& driver_url);

  // Drivers we cached from the DriverIndex.
  fbl::DoublyLinkedList<std::unique_ptr<Driver>> driver_index_drivers_;

  internal::BasePackageResolver base_resolver_;
  std::optional<std::thread> system_loading_thread_;
  Coordinator* coordinator_ = nullptr;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_LOADER_H_

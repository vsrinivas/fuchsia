// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_LOADER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_LOADER_H_

#include <fuchsia/driver/framework/llcpp/fidl.h>
#include <threads.h>

#include <fbl/intrusive_double_list.h>

#include "src/devices/bin/driver_manager/base_package_resolver.h"
#include "src/devices/bin/driver_manager/device.h"
#include "src/devices/bin/driver_manager/driver.h"

class Coordinator;

namespace fdf = fuchsia_driver_framework;

class DriverLoader {
 public:
  // Takes in an unowned connection to boot arguments. boot_args must outlive DriverLoader.
  // Takes in an unowned connection to base_resolver. base_resolver must outlive DriverLoader.
  explicit DriverLoader(fidl::WireSyncClient<fuchsia_boot::Arguments>* boot_args,
                        fidl::WireSharedClient<fdf::DriverIndex> driver_index,
                        internal::PackageResolverInterface* base_resolver, bool require_system)
      : base_resolver_(base_resolver),
        driver_index_(std::move(driver_index)),
        include_fallback_drivers_(!require_system) {}

  ~DriverLoader();

  // Start a Thread to service loading drivers.
  // DriverLoader will join this thread when it destructs.
  // `coordinator_` is not thread safe, so any calls to it must be made on the
  // `coordinator_->dispatcher()` thread.
  void StartSystemLoadingThread(Coordinator* coordinator);

  // This will schedule a task on the async_dispatcher that will return
  // when DriverIndex has loaded the base drivers. When the task completes,
  // `callback` will be called.
  void WaitForBaseDrivers(fit::callback<void()> callback);
  std::vector<const Driver*> MatchDeviceDriverIndex(const fbl::RefPtr<Device>& dev,
                                                    std::string_view libname = "");
  std::vector<const Driver*> MatchPropertiesDriverIndex(
      fidl::VectorView<fdf::wire::NodeProperty> props, std::string_view libname = "");

  const Driver* LibnameToDriver(std::string_view libname) const;

 private:
  bool MatchesLibnameDriverIndex(const std::string& driver_url, std::string_view libname);

  const Driver* LoadDriverUrlDriverIndex(const std::string& driver_url);

  // Drivers we cached from the DriverIndex.
  fbl::DoublyLinkedList<std::unique_ptr<Driver>> driver_index_drivers_;

  internal::PackageResolverInterface* base_resolver_;
  std::optional<std::thread> system_loading_thread_;
  fidl::WireSharedClient<fdf::DriverIndex> driver_index_;

  // When this is true we will return DriverIndex fallback drivers.
  // This is true after the system is loaded (or if require_system is false)
  bool include_fallback_drivers_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_LOADER_H_

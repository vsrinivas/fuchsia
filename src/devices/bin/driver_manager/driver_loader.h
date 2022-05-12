// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_LOADER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_LOADER_H_

#include <fidl/fuchsia.driver.development/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
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
                        internal::PackageResolverInterface* base_resolver,
                        async_dispatcher_t* dispatcher, bool require_system,
                        internal::PackageResolverInterface* universe_resolver)
      : base_resolver_(base_resolver),
        driver_index_(std::move(driver_index)),
        include_fallback_drivers_(!require_system),
        universe_resolver_(universe_resolver) {}

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

  struct MatchDeviceConfig {
    std::string_view libname;
    // This config should only be true after the base drivers are loaded.
    // We will need to go through all the devices and bind just base drivers
    // and fallback drivers.
    bool only_return_base_and_fallback_drivers = false;
  };

  zx_status_t AddDeviceGroup(std::string_view topological_path,
                             fidl::VectorView<fdf::wire::DeviceGroupNode> nodes);

  const std::vector<MatchedDriver> MatchDeviceDriverIndex(const fbl::RefPtr<Device>& dev,
                                                          const MatchDeviceConfig& config);
  const std::vector<MatchedDriver> MatchPropertiesDriverIndex(
      fidl::VectorView<fdf::wire::NodeProperty> props, const MatchDeviceConfig& config);

  const Driver* LibnameToDriver(std::string_view libname) const;

  const Driver* LoadDriverUrl(const std::string& driver_url, bool use_universe_resolver = false);

  zx::status<std::vector<fuchsia_driver_development::wire::DriverInfo>> GetDriverInfo(
      fidl::AnyArena& allocator, fidl::VectorView<fidl::StringView> filter);

  // This API is used for debugging, for GetDriverInfo and DumpDrivers.
  std::vector<const Driver*> GetAllDriverIndexDrivers();

 private:
  bool MatchesLibnameDriverIndex(const std::string& driver_url, std::string_view libname);

  // Drivers we cached from the DriverIndex.
  fbl::DoublyLinkedList<std::unique_ptr<Driver>> driver_index_drivers_;

  internal::PackageResolverInterface* base_resolver_;
  std::optional<std::thread> system_loading_thread_;
  fidl::WireSharedClient<fdf::DriverIndex> driver_index_;

  // When this is true we will return DriverIndex fallback drivers.
  // This is true after the system is loaded (or if require_system is false)
  bool include_fallback_drivers_;

  // The universe package resolver.
  // Currently used only for ephemeral drivers.
  internal::PackageResolverInterface* universe_resolver_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_LOADER_H_

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver_loader.h"

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <sched.h>
#include <unistd.h>
#include <zircon/threads.h>

#include <thread>

#include <fbl/unique_fd.h>

#include "coordinator.h"
#include "src/devices/bin/driver_manager/manifest_parser.h"
#include "src/devices/lib/log/log.h"

DriverLoader::~DriverLoader() {
  if (system_loading_thread_) {
    system_loading_thread_->join();
  }
}

void DriverLoader::StartSystemLoadingThread() {
  if (system_loading_thread_) {
    LOGF(ERROR, "DriverLoader: StartLoadingThread cannot be called twice!\n");
    return;
  }

  system_loading_thread_ = std::thread([coordinator = coordinator_]() {
    fbl::unique_fd fd(open("/system", O_RDONLY));
    if (fd.get() < 0) {
      LOGF(WARNING, "Unable to open '/system', system drivers are disabled");
      return;
    }

    fbl::DoublyLinkedList<std::unique_ptr<Driver>> drivers;

    auto driver_added = [&drivers](Driver* drv, const char* version) {
      std::unique_ptr<Driver> driver(drv);
      LOGF(INFO, "Adding driver '%s' '%s'", driver->name.data(), driver->libname.data());
      if (load_vmo(driver->libname.data(), &driver->dso_vmo)) {
        LOGF(ERROR, "Driver '%s' '%s' could not cache DSO", driver->name.data(),
             driver->libname.data());
      }
      // De-prioritize drivers that are "fallback".
      if (driver->fallback) {
        drivers.push_back(std::move(driver));
      } else {
        drivers.push_front(std::move(driver));
      }
    };

    find_loadable_drivers(coordinator->boot_args(), "/system/driver", driver_added);

    async::PostTask(coordinator->dispatcher(),
                    [coordinator = coordinator, drivers = std::move(drivers)]() mutable {
                      coordinator->AddAndBindDrivers(std::move(drivers));
                      coordinator->BindFallbackDrivers();
                    });
  });

  constexpr char name[] = "driver-loader-thread";
  zx_object_set_property(native_thread_get_zx_handle(system_loading_thread_->native_handle()),
                         ZX_PROP_NAME, name, sizeof(name));
}

const Driver* DriverLoader::LibnameToDriver(std::string_view libname) const {
  for (const auto& drv : driver_index_drivers_) {
    if (libname.compare(drv.libname) == 0) {
      return &drv;
    }
  }
  return nullptr;
}

const Driver* DriverLoader::LoadDriverUrlDriverIndex(const std::string& driver_url) {
  // Check if we've already loaded this driver. If we have then return it.
  auto driver = LibnameToDriver(driver_url);
  if (driver != nullptr) {
    return driver;
  }

  // We've never seen the driver before so add it, then return it.
  auto fetched_driver = base_resolver_.FetchDriver(driver_url);
  if (fetched_driver.is_error()) {
    LOGF(ERROR, "Error fetching driver: %s: %d", driver_url.data(), fetched_driver.error_value());
    return nullptr;
  }
  driver_index_drivers_.push_back(std::move(fetched_driver.value()));

  return &driver_index_drivers_.back();
}

bool DriverLoader::MatchesLibnameDriverIndex(const std::string& driver_url,
                                             std::string_view libname) {
  if (libname.compare(driver_url) == 0) {
    return true;
  }

  auto result = GetPathFromUrl(driver_url);
  if (result.is_error()) {
    return false;
  }

  return result.value().compare(libname) == 0;
}

std::vector<const Driver*> DriverLoader::MatchDeviceDriverIndex(const fbl::RefPtr<Device>& dev,
                                                                std::string_view libname) {
  std::vector<const Driver*> matched_drivers;

  if (!coordinator_->driver_index().is_valid()) {
    return matched_drivers;
  }

  bool autobind = libname.empty();

  fidl::FidlAllocator allocator;
  auto& props = dev->props();
  fdf::wire::NodeAddArgs args(allocator);
  fidl::VectorView<fdf::wire::NodeProperty> fidl_props(allocator, props.size() + 2);

  fidl_props[0] = fdf::wire::NodeProperty(allocator)
                      .set_key(allocator, BIND_PROTOCOL)
                      .set_value(allocator, dev->protocol_id());
  fidl_props[1] = fdf::wire::NodeProperty(allocator)
                      .set_key(allocator, BIND_AUTOBIND)
                      .set_value(allocator, autobind);
  for (size_t i = 0; i < props.size(); i++) {
    fidl_props[i + 2] = fdf::wire::NodeProperty(allocator)
                            .set_key(allocator, props[i].id)
                            .set_value(allocator, props[i].value);
  }
  args.set_properties(allocator, std::move(fidl_props));

  auto result = coordinator_->driver_index()->MatchDriversV1_Sync(std::move(args));
  if (!result.ok()) {
    if (result.status() == ZX_ERR_PEER_CLOSED) {
      return matched_drivers;
    }

    // If DriverManager can't connect initially then this will return CANCELED.
    // This is normal if driver-index isn't included in the build.
    if (result.status() != ZX_ERR_CANCELED) {
      LOGF(ERROR, "DriverIndex: MatchDriver failed: %s", result.status_string());
    }
    return matched_drivers;
  }
  // If there's no driver to match then DriverIndex will return ZX_ERR_NOT_FOUND.
  if (result->result.is_err()) {
    if (result->result.err() != ZX_ERR_NOT_FOUND) {
      LOGF(ERROR, "DriverIndex: MatchDriver returned error: %d", result->result.err());
    }
    return matched_drivers;
  }

  const auto& drivers = result->result.response().drivers;

  for (auto driver : drivers) {
    if (!driver.has_driver_url()) {
      LOGF(ERROR, "DriverIndex: MatchDriver response did not have a driver_url");
      continue;
    }
    std::string driver_url(driver.driver_url().get());
    auto loaded_driver = LoadDriverUrlDriverIndex(driver_url);
    if (libname.empty() || MatchesLibnameDriverIndex(driver_url, libname)) {
      matched_drivers.push_back(loaded_driver);
    }
  }
  return matched_drivers;
}

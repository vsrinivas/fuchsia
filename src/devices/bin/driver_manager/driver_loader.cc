// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver_loader.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <sched.h>
#include <unistd.h>
#include <zircon/threads.h>

#include <thread>
#include <variant>

#include <fbl/unique_fd.h>

#include "coordinator.h"
#include "src/devices/bin/driver_manager/manifest_parser.h"
#include "src/devices/lib/log/log.h"

DriverLoader::~DriverLoader() {
  if (system_loading_thread_) {
    system_loading_thread_->join();
  }
}

void DriverLoader::StartSystemLoadingThread(Coordinator* coordinator) {
  if (system_loading_thread_) {
    LOGF(ERROR, "DriverLoader: StartLoadingThread cannot be called twice!\n");
    return;
  }

  system_loading_thread_ = std::thread([coordinator]() {
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

void DriverLoader::WaitForBaseDrivers(fit::callback<void()> callback) {
  // TODO(dgilhooley): Change this back to an ERROR once DriverIndex is used in all tests.
  if (!driver_index_.is_valid()) {
    LOGF(INFO, "%s: DriverIndex is not initialized", __func__);
    return;
  }

  driver_index_->WaitForBaseDrivers(
      [this, callback = std::move(callback)](
          fidl::WireUnownedResult<fdf::DriverIndex::WaitForBaseDrivers>&& result) mutable {
        if (!result.ok()) {
          LOGF(ERROR, "Failed to connect to DriverIndex: %s",
               result.error().FormatDescription().c_str());
          return;
        }
        include_fallback_drivers_ = true;
        callback();
      });
}

const Driver* DriverLoader::LoadDriverUrl(const std::string& driver_url) {
  // Check if we've already loaded this driver. If we have then return it.
  auto driver = LibnameToDriver(driver_url);
  if (driver != nullptr) {
    return driver;
  }

  // We've never seen the driver before so add it, then return it.
  auto fetched_driver = base_resolver_->FetchDriver(driver_url);
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
                                                                const MatchDeviceConfig& config) {
  if (!driver_index_.is_valid()) {
    return std::vector<const Driver*>();
  }

  bool autobind = config.libname.empty();

  fidl::Arena allocator;
  auto& props = dev->props();
  auto& str_props = dev->str_props();
  fidl::VectorView<fdf::wire::NodeProperty> fidl_props(allocator,
                                                       props.size() + str_props.size() + 2);

  size_t index = 0;
  fidl_props[index++] =
      fdf::wire::NodeProperty(allocator)
          .set_key(allocator, fdf::wire::NodePropertyKey::WithIntValue(allocator, BIND_PROTOCOL))
          .set_value(allocator,
                     fdf::wire::NodePropertyValue::WithIntValue(allocator, dev->protocol_id()));
  fidl_props[index++] =
      fdf::wire::NodeProperty(allocator)
          .set_key(allocator, fdf::wire::NodePropertyKey::WithIntValue(allocator, BIND_AUTOBIND))
          .set_value(allocator, fdf::wire::NodePropertyValue::WithIntValue(allocator, autobind));

  for (size_t i = 0; i < props.size(); i++) {
    fidl_props[index++] =
        fdf::wire::NodeProperty(allocator)
            .set_key(allocator, fdf::wire::NodePropertyKey::WithIntValue(allocator, props[i].id))
            .set_value(allocator,
                       fdf::wire::NodePropertyValue::WithIntValue(allocator, props[i].value));
  }

  for (size_t i = 0; i < str_props.size(); i++) {
    auto prop = fdf::wire::NodeProperty(allocator).set_key(
        allocator,
        fdf::wire::NodePropertyKey::WithStringValue(allocator, allocator, str_props[i].key));
    if (std::holds_alternative<uint32_t>(str_props[i].value)) {
      prop.set_value(allocator, fdf::wire::NodePropertyValue::WithIntValue(
                                    allocator, std::get<uint32_t>(str_props[i].value)));
    } else if (std::holds_alternative<std::string>(str_props[i].value)) {
      prop.set_value(allocator,
                     fdf::wire::NodePropertyValue::WithStringValue(
                         allocator, allocator, std::get<std::string>(str_props[i].value)));
    } else if (std::holds_alternative<bool>(str_props[i].value)) {
      prop.set_value(allocator, fdf::wire::NodePropertyValue::WithBoolValue(
                                    allocator, std::get<bool>(str_props[i].value)));
    }
    fidl_props[index++] = std::move(prop);
  }

  return MatchPropertiesDriverIndex(fidl_props, config);
}

std::vector<const Driver*> DriverLoader::MatchPropertiesDriverIndex(
    fidl::VectorView<fdf::wire::NodeProperty> props, const MatchDeviceConfig& config) {
  std::vector<const Driver*> matched_drivers;
  std::vector<const Driver*> matched_fallback_drivers;
  if (!driver_index_.is_valid()) {
    return matched_drivers;
  }

  fidl::Arena allocator;
  fdf::wire::NodeAddArgs args(allocator);
  args.set_properties(allocator, std::move(props));

  auto result = driver_index_->MatchDriversV1_Sync(std::move(args));
  if (!result.ok()) {
    if (result.status() != ZX_OK) {
      LOGF(ERROR, "DriverIndex::MatchDriversV1 failed: %d", result.status());
      return matched_drivers;
    }
  }
  // If there's no driver to match then DriverIndex will return ZX_ERR_NOT_FOUND.
  if (result->result.is_err()) {
    if (result->result.err() != ZX_ERR_NOT_FOUND) {
      LOGF(ERROR, "DriverIndex: MatchDriversV1 returned error: %d", result->result.err());
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

    auto loaded_driver = LoadDriverUrl(driver_url);
    if (!loaded_driver) {
      continue;
    }

    if (config.only_return_base_and_fallback_drivers) {
      if (IsFuchsiaBootScheme(driver_url) && !loaded_driver->fallback) {
        continue;
      }
    }

    if (config.libname.empty() || MatchesLibnameDriverIndex(driver_url, config.libname)) {
      if (loaded_driver->fallback) {
        if (include_fallback_drivers_ || !config.libname.empty()) {
          matched_fallback_drivers.push_back(loaded_driver);
        }
      } else {
        matched_drivers.push_back(loaded_driver);
      }
    }
  }

  matched_drivers.insert(matched_drivers.end(), matched_fallback_drivers.begin(),
                         matched_fallback_drivers.end());
  return matched_drivers;
}

std::vector<const Driver*> DriverLoader::GetAllDriverIndexDrivers() {
  std::vector<const Driver*> drivers;

  auto driver_index_client = service::Connect<fuchsia_driver_development::DriverIndex>();
  if (driver_index_client.is_error()) {
    LOGF(WARNING, "Failed to connect to fuchsia_driver_development::DriverIndex\n");
    return drivers;
  }

  auto driver_index = fidl::WireSharedClient<fuchsia_driver_development::DriverIndex>(
      std::move(driver_index_client.value()), dispatcher_);
  auto info_result = driver_index->GetDriverInfo_Sync(fidl::VectorView<fidl::StringView>());

  // There are still some environments where we can't connect to DriverIndex.
  if (info_result.status() != ZX_OK) {
    LOGF(INFO, "DriverIndex:GetDriverInfo failed: %d\n", info_result.status());
    return drivers;
  }
  if (info_result->result.is_err()) {
    LOGF(ERROR, "GetDriverInfo failed: %d\n", info_result->result.err());
    return drivers;
  }
  for (auto driver : info_result->result.response().drivers) {
    if (!driver.has_url()) {
      continue;
    }
    std::string url(driver.url().data(), driver.url().size());
    const Driver* drv = LoadDriverUrl(url);
    if (drv) {
      drivers.push_back(drv);
    }
  }

  return drivers;
}

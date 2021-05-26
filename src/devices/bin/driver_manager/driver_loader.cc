// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver_loader.h"

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <sched.h>
#include <threads.h>
#include <unistd.h>

#include <fbl/unique_fd.h>

#include "coordinator.h"
#include "src/devices/bin/driver_manager/manifest_parser.h"
#include "src/devices/lib/log/log.h"

DriverLoader::~DriverLoader() {
  if (loading_thread_) {
    thrd_join(loading_thread_.value(), nullptr);
  }
}

void DriverLoader::StartLoadingThread(Coordinator* coordinator) {
  if (loading_thread_) {
    LOGF(ERROR, "DriverLoader: StartLoadingThread cannot be called twice!\n");
    return;
  }
  coordinator_ = coordinator;

  thrd_t t;
  int ret = thrd_create_with_name(
      &t,
      [](void* arg) {
        reinterpret_cast<DriverLoader*>(arg)->LoadDrivers();
        return 0;
      },
      this, "driver-loader-thread");

  if (ret != thrd_success) {
    LOGF(ERROR, "DriverLoader: starting a new thread failed!\n");
    return;
  }

  loading_thread_ = t;
}

void DriverLoader::LoadDrivers() {
  fbl::unique_fd fd(open("/system", O_RDONLY));
  if (fd.get() < 0) {
    LOGF(WARNING, "Unable to open '/system', system drivers are disabled");
    return;
  }

  find_loadable_drivers(coordinator_->boot_args(), "/system/driver",
                        fit::bind_member(this, &DriverLoader::DriverAdded));
  async::PostTask(coordinator_->dispatcher(),
                  [coordinator = coordinator_, drivers = std::move(drivers_)]() mutable {
                    coordinator->AddAndBindDrivers(std::move(drivers));
                    coordinator->BindFallbackDrivers();
                  });

  auto manifest_path = GetPathFromUrl(
      "fuchsia-pkg://fuchsia.com/driver-manager-base-config#config/base-driver-manifest.json");
  ZX_DEBUG_ASSERT(manifest_path.is_ok());
  fd = fbl::unique_fd(open(manifest_path.value().c_str(), O_RDONLY));
  if (fd.get() < 0) {
    LOGF(WARNING, "Unable to open Base Manifest, base drivers are disabled");
    return;
  }

  auto manifest_result = ParseDriverManifestFromPath(manifest_path.value());
  if (manifest_result.is_error()) {
    LOGF(ERROR, "Base Driver Manifest failed to parse: %s", manifest_result.status_string());
    return;
  }

  std::vector<DriverManifestEntry> drivers = std::move(manifest_result.value());
  for (auto& driver : drivers) {
    auto result = base_resolver_.FetchDriver(driver.driver_url);
    if (result.status_value() != ZX_OK) {
      LOGF(ERROR, "Failed to fetch %s: %s", driver.driver_url.c_str(), result.status_string());
      continue;
    }
    LOGF(INFO, "Adding driver '%s' '%s'", result.value()->name.data(), driver.driver_url.c_str());
    drivers_.push_back(std::move(result.value()));
  }
  async::PostTask(coordinator_->dispatcher(),
                  [coordinator = coordinator_, drivers = std::move(drivers_)]() mutable {
                    coordinator->AddAndBindDrivers(std::move(drivers));
                    coordinator->BindFallbackDrivers();
                  });
}

void DriverLoader::DriverAdded(Driver* drv, const char* version) {
  std::unique_ptr<Driver> driver(drv);
  LOGF(INFO, "Adding driver '%s' '%s'", driver->name.data(), driver->libname.data());
  if (load_vmo(driver->libname.data(), &driver->dso_vmo)) {
    LOGF(ERROR, "Driver '%s' '%s' could not cache DSO", driver->name.data(),
         driver->libname.data());
  }
  if (version[0] == '*') {
    // de-prioritize drivers that are "fallback"
    drivers_.push_back(std::move(driver));
  } else {
    drivers_.push_front(std::move(driver));
  }
}

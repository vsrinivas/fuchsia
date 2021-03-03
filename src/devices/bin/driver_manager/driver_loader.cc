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
  coordinator_ = coordinator;
}

void DriverLoader::LoadDrivers() {
  fbl::unique_fd fd(open("/system-delayed", O_RDONLY));
  if (fd.get() < 0) {
    LOGF(WARNING, "Unabled to open '/system-delayed', system drivers are disabled");
    return;
  }

  find_loadable_drivers("/system/driver", fit::bind_member(this, &DriverLoader::DriverAdded));
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

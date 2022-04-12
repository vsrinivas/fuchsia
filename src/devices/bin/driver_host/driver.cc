// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_host/driver.h"

#include <lib/fdf/cpp/dispatcher.h>

#include <fbl/ref_ptr.h>

#include "lib/fdf/dispatcher.h"
#include "src/devices/bin/driver_host/driver_stack_manager.h"

zx::status<fbl::RefPtr<Driver>> Driver::Create(zx_driver_t* zx_driver) {
  auto driver = fbl::MakeRefCounted<Driver>(zx_driver);

  DriverStackManager dsm(driver.get());

  auto dispatcher = fdf::Dispatcher::Create(
      FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS,
      [](fdf_dispatcher_t* dispatcher) { fdf_dispatcher_destroy(dispatcher); });
  if (dispatcher.is_error()) {
    return dispatcher.take_error();
  }

  driver->dispatcher_ = *std::move(dispatcher);
  return zx::ok(std::move(driver));
}

Driver::~Driver() {
  // TODO(fxbug.dev/97755): ideally we would want to force shutdown the dispatchers
  // before calling the device ReleaseOp, but that doesn't work currently if we are
  // sharing the same dispatcher for all devices in a driver.
  ZX_ASSERT(!fdf_internal_dispatcher_has_queued_tasks(dispatcher_.get()));
  dispatcher_.ShutdownAsync();
  // The dispatcher will be destroyed in the callback.
  dispatcher_.release();
}

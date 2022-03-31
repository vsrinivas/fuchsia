// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_host/driver.h"

#include <lib/fdf/cpp/dispatcher.h>

#include <fbl/ref_ptr.h>

#include "src/devices/bin/driver_host/driver_stack_manager.h"

zx::status<fbl::RefPtr<Driver>> Driver::Create(zx_driver_t* zx_driver) {
  auto driver = fbl::MakeRefCounted<Driver>(zx_driver);

  DriverStackManager dsm(driver.get());
  auto dispatcher = fdf::Dispatcher::Create(0);
  if (dispatcher.is_error()) {
    return dispatcher.take_error();
  }

  driver->dispatcher_ = *std::move(dispatcher);
  return zx::ok(std::move(driver));
}

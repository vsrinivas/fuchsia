// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_host/driver.h"

#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/cpp/env.h>

#include <fbl/ref_ptr.h>
#include <fbl/string_printf.h>

#include "lib/fdf/dispatcher.h"

zx::status<fbl::RefPtr<Driver>> Driver::Create(zx_driver_t* zx_driver) {
  auto driver = fbl::MakeRefCounted<Driver>(zx_driver);

  auto dispatcher = fdf_env::DispatcherBuilder::CreateWithOwner(
      driver.get(), FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS,
      fbl::StringPrintf("%s-default-%p", zx_driver->name(), driver.get()),
      [driver = driver.get()](fdf_dispatcher_t* dispatcher) { driver->released_.Signal(); });
  if (dispatcher.is_error()) {
    return dispatcher.take_error();
  }

  driver->dispatcher_ = *std::move(dispatcher);
  return zx::ok(std::move(driver));
}

Driver::~Driver() {
  // Generally, we will shut down the dispatcher when the last device associated with
  // the driver is unbound.
  // However in some tests we don't properly tear down devices so we also shut down here.
  ZX_ASSERT(!fdf_env_dispatcher_has_queued_tasks(dispatcher_.get()));
  ZX_ASSERT(device_count_ == 0);
  dispatcher_.ShutdownAsync();
  released_.Wait();
}

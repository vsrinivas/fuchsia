// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_DRIVER_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_DRIVER_H_

#include <lib/ddk/driver.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/zx/status.h>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/devices/bin/driver_host/zx_driver.h"

// Per driver instance unique context. Primarily use for tracking the default driver runtime
// dispatcher.
class Driver : public fbl::RefCounted<Driver> {
 public:
  // |zx_driver| must outlive |Driver|.
  static zx::status<fbl::RefPtr<Driver>> Create(zx_driver_t* zx_driver);

  explicit Driver(zx_driver_t* zx_driver) : zx_driver_(zx_driver) {}

  // No copy, no move.
  Driver(const Driver&) = delete;
  Driver& operator=(const Driver&) = delete;
  Driver(Driver&&) = delete;
  Driver& operator=(Driver&&) = delete;

  ~Driver() = default;

  zx_driver_t* zx_driver() const { return zx_driver_; }

  fdf::UnownedDispatcher dispatcher() { return dispatcher_.borrow(); }

 private:
  zx_driver_t* zx_driver_;

  fdf::Dispatcher dispatcher_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_DRIVER_H_

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_DRIVER_STACK_MANAGER_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_DRIVER_STACK_MANAGER_H_

#include <lib/fdf/internal.h>

// RAII wrapper for updating the runtime driver call stack.
class DriverStackManager {
 public:
  explicit DriverStackManager(const void* driver) { fdf_internal_push_driver(driver); }
  ~DriverStackManager() { fdf_internal_pop_driver(); }
};

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_DRIVER_STACK_MANAGER_H_

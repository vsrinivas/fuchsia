// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DRIVER2_RECORD_H_
#define SRC_DEVICES_LIB_DRIVER2_RECORD_H_

#include <lib/async/dispatcher.h>
#include <zircon/fidl.h>

struct DriverRecordV1 {
  // This is the version of `DriverRecord` and all structures used by it.
  uint64_t version;

  // Pointer to a function that can start execution of the driver. This
  // function is executed on the shared driver thread within a `driver_host`.
  //
  // |msg| contains a `fuchsia.driver.framework.DriverStartArgs` table. The
  // table is "moved" to the driver, and is then presumed to be owned by it.
  // |dispatcher| is the default async dispatcher on which to run the driver.
  // The driver is free to ignore this and use its own.
  // |driver| provides a place to store the opaque driver structure.
  zx_status_t (*start)(fidl_msg_t* msg, async_dispatcher_t* dispatcher, void** driver);

  // Pointer to a function that can stop execution of the driver. This function
  // is executed on the shared driver thread within a `driver_host`.
  //
  // |driver| is the value that was stored when the driver was started.
  zx_status_t (*stop)(void* driver);
};

#define FUCHSIA_DRIVER_RECORD_V1(start, stop) \
  extern "C" const DriverRecordV1 __fuchsia_driver_record__ __EXPORT { .version = 1, start, stop, }

#endif  // SRC_DEVICES_LIB_DRIVER2_RECORD_H_

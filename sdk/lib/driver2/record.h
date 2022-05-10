// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER2_RECORD_H_
#define LIB_DRIVER2_RECORD_H_

#include <lib/fdf/dispatcher.h>
#include <zircon/fidl.h>

struct EncodedDriverStartArgs {
  // |msg| is an encoded `fuchsia.driver.framework/DriverStartArgs` table. The
  // ownership of handles in |msg| are transferred to the driver. The driver may
  // mutate the bytes referenced by |msg|, but those are only alive until the
  // |DriverRecordV1::start| method returns.
  fidl_incoming_msg_t* msg;

  // |wire_format_metadata| describes the the revision of the FIDL wire format
  // used to encode |msg|.
  fidl_opaque_wire_format_metadata wire_format_metadata;
};

struct DriverRecordV1 {
  // This is the version of `DriverRecord` and all structures used by it.
  uint64_t version;

  // Pointer to a function that can start execution of the driver. This
  // function is executed on the shared driver thread within a `driver_host`.
  //
  // |start_args| contains the arguments for starting a driver.
  // |dispatcher| is the default fdf dispatcher on which to run the driver.
  // The driver is free to ignore this and use its own.
  // |driver| provides a place to store the opaque driver structure.
  zx_status_t (*start)(EncodedDriverStartArgs start_args, fdf_dispatcher_t* dispatcher,
                       void** driver);

  // Pointer to a function that can stop execution of the driver. This function
  // is executed on the shared driver thread within a `driver_host`.
  //
  // |driver| is the value that was stored when the driver was started.
  zx_status_t (*stop)(void* driver);
};

#define FUCHSIA_DRIVER_RECORD_V1(start, stop) \
  extern "C" const DriverRecordV1 __fuchsia_driver_record__ __EXPORT { .version = 1, start, stop, }

#endif  // LIB_DRIVER2_RECORD_H_

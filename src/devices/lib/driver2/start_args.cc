// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/driver2/start_args.h"

namespace fdf = llcpp::fuchsia::driver::framework;

namespace start_args {

zx::status<fidl_msg_t> Encode(Storage* storage, fdf::DriverStartArgs start_args,
                              const char** error) {
  fidl_msg_t msg = {
      .bytes = storage->bytes,
      .handles = storage->handles,
      .num_bytes = sizeof(storage->bytes),
      .num_handles = fdf::DriverStartArgs::MaxNumHandles,
  };
  zx_status_t status = fidl_linearize_and_encode_msg(fdf::DriverStartArgs::Type, &start_args, &msg,
                                                     &msg.num_bytes, &msg.num_handles, error);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(msg);
}

zx::status<fdf::DriverStartArgs*> Decode(fidl_msg_t* msg, const char** error) {
  zx_status_t status = fidl_decode_msg(fdf::DriverStartArgs::Type, msg, error);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(reinterpret_cast<fdf::DriverStartArgs*>(msg->bytes));
}

}  // namespace start_args

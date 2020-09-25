// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DRIVER2_START_ARGS_H_
#define SRC_DEVICES_LIB_DRIVER2_START_ARGS_H_

#include <fuchsia/driver/framework/llcpp/fidl.h>
#include <lib/zx/status.h>

namespace start_args {

// Stores a DriverStartArgs table, in order to pass it from a driver host to a
// driver in a language-agnostic way.
struct Storage {
  FIDL_ALIGNDECL
  uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[llcpp::fuchsia::driver::framework::DriverStartArgs::MaxNumHandles];
};

// Encode |start_args| into |storage|.
zx::status<fidl_msg_t> Encode(Storage* storage,
                              llcpp::fuchsia::driver::framework::DriverStartArgs start_args,
                              const char** error);

// Decode |msg| and return a DriverStartArgs.
zx::status<llcpp::fuchsia::driver::framework::DriverStartArgs*> Decode(fidl_msg_t* msg,
                                                                       const char** error);

}  // namespace start_args

#endif  // SRC_DEVICES_LIB_DRIVER2_START_ARGS_H_

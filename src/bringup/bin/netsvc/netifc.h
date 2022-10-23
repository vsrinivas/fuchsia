// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_NETSVC_NETIFC_H_
#define SRC_BRINGUP_BIN_NETSVC_NETIFC_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/stdcompat/span.h>
#include <lib/stdcompat/string_view.h>
#include <lib/zx/result.h>
#include <stdbool.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "src/connectivity/lib/network-device/cpp/network_device_client.h"

struct eth_buffer;

class DeviceBuffer {
 public:
  using Contents =
      std::variant<std::monostate, eth_buffer*, network::client::NetworkDeviceClient::Buffer>;
  explicit DeviceBuffer(Contents contents);
  cpp20::span<uint8_t> data();
  zx_status_t Send(size_t len);
  static zx::result<DeviceBuffer> Get(size_t len, bool block);

 private:
  Contents contents_;
};

// Setup networking.
//
// If non-empty, `interface` holds the topological path of the interface
// intended to use for networking.
zx::result<> netifc_open(async_dispatcher_t* dispatcher, cpp17::string_view interface,
                         fit::callback<void(zx_status_t)> on_error);

// Return nonzero if interface exists.
int netifc_active();

// Shut down networking.
void netifc_close();

void netifc_recv(async_dispatcher_t* dispatcher, void* data, size_t len);

#endif  // SRC_BRINGUP_BIN_NETSVC_NETIFC_H_

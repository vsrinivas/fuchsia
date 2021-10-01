// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_PROXY_DEVICE_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_PROXY_DEVICE_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/ddk/driver.h>
#include <lib/fidl/llcpp/client_end.h>

#include "src/devices/bin/driver_host/zx_device.h"

// Modifies |device| to have the appropriate protocol_id, ctx, and ops tables
// for a proxy device
void InitializeProxyDevice(const fbl::RefPtr<zx_device>& device,
                           fidl::ClientEnd<fuchsia_io::Directory> incoming_dir);

// Returns a zx_driver instance for proxy devices
fbl::RefPtr<zx_driver> GetProxyDriver(DriverHostContext* ctx);

class ProxyDevice : public fbl::RefCounted<ProxyDevice> {
 public:
  explicit ProxyDevice(fbl::RefPtr<zx_device> device) : device_(std::move(device)) {}
  ~ProxyDevice() = default;

  zx::status<> ConnectToProtocol(const char* protocol, zx::channel request);

 private:
  fbl::RefPtr<zx_device> device_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_PROXY_DEVICE_H_

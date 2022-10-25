// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_NETWORK_DEVICE_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_NETWORK_DEVICE_H_

#include <fidl/fuchsia.hardware.network/cpp/wire.h>
#include <fuchsia/hardware/network/mac/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/driver.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>

#include "device/public/network_device.h"

namespace network {

class NetworkDevice;
using DeviceType =
    ddk::Device<NetworkDevice, ddk::Messageable<fuchsia_hardware_network::DeviceInstance>::Mixin,
                ddk::Unbindable>;

class NetworkDevice : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_NETWORK_DEVICE> {
 public:
  explicit NetworkDevice(zx_device_t* parent, async_dispatcher_t* dispatcher)
      : DeviceType(parent), dispatcher_(dispatcher) {}
  ~NetworkDevice() override;

  static zx_status_t Create(void* ctx, zx_device_t* parent, async_dispatcher_t* dispatcher);

  void DdkUnbind(ddk::UnbindTxn unbindTxn);

  void DdkRelease();

  void GetDevice(GetDeviceRequestView request, GetDeviceCompleter::Sync& _completer) override;

 private:
  async_dispatcher_t* dispatcher_;
  std::unique_ptr<NetworkDeviceInterface> device_;
};
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_NETWORK_DEVICE_H_

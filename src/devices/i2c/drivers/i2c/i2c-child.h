// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_I2C_DRIVERS_I2C_I2C_CHILD_H_
#define SRC_DEVICES_I2C_DRIVERS_I2C_I2C_CHILD_H_

#include <fidl/fuchsia.hardware.i2c.businfo/cpp/wire.h>
#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <fuchsia/hardware/i2cimpl/cpp/banjo.h>
#include <lib/ddk/platform-defs.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/svc/outgoing.h>

#include <optional>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>

#include "i2c-bus.h"
#include "lib/fdf/dispatcher.h"

namespace i2c {

namespace fidl_i2c = fuchsia_hardware_i2c;

class I2cChild;
using I2cChildType = ddk::Device<I2cChild, ddk::Messageable<fidl_i2c::Device>::Mixin>;

class I2cChild : public I2cChildType {
 public:
  I2cChild(zx_device_t* parent, fbl::RefPtr<I2cBus> bus, uint16_t address)
      : I2cChildType(parent), bus_(std::move(bus)), address_(address) {}

  static zx_status_t CreateAndAddDevice(
      zx_device_t* parent, const fuchsia_hardware_i2c_businfo::wire::I2CChannel& channel,
      const fbl::RefPtr<I2cBus>& bus, async_dispatcher_t* dispatcher);

  void DdkRelease() { delete this; }

  void Transfer(TransferRequestView request, TransferCompleter::Sync& completer) override;

 private:
  void Bind(fidl::ServerEnd<fidl_i2c::Device> request) {
    fidl::BindServer<fidl::WireServer<fidl_i2c::Device>>(
        fdf::Dispatcher::GetCurrent()->async_dispatcher(), std::move(request), this);
  }

  std::optional<svc::Outgoing> outgoing_dir_;

  fbl::RefPtr<I2cBus> bus_;
  const uint16_t address_;
};

}  // namespace i2c

#endif  // SRC_DEVICES_I2C_DRIVERS_I2C_I2C_CHILD_H_

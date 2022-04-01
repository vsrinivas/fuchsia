// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_I2C_DRIVERS_I2C_I2C_CHILD_H_
#define SRC_DEVICES_I2C_DRIVERS_I2C_I2C_CHILD_H_

#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <fuchsia/hardware/i2c/cpp/banjo.h>
#include <fuchsia/hardware/i2cimpl/cpp/banjo.h>
#include <lib/ddk/platform-defs.h>
#include <lib/svc/outgoing.h>

#include <optional>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>

#include "i2c-bus.h"

namespace i2c {

namespace fidl_i2c = fuchsia_hardware_i2c;

// TODO(fxbug.dev/96293): Merge I2cFidlChild back into I2cChild and delete I2cBanjoChild once all
// clients are using FIDL.
class I2cChild {
 public:
  I2cChild(fbl::RefPtr<I2cBus> bus, uint16_t address) : bus_(std::move(bus)), address_(address) {}

  static zx_status_t CreateAndAddDevice(zx_device_t* parent,
                                        const fidl_i2c::wire::I2CChannel& channel,
                                        const fbl::RefPtr<I2cBus>& bus,
                                        async_dispatcher_t* dispatcher);

 protected:
  // To be called by I2cFidlChild and I2cBanjoChild.
  void Transfer(fidl::WireServer<fidl_i2c::Device2>::TransferRequestView request,
                fidl::WireServer<fidl_i2c::Device2>::TransferCompleter::Sync& completer);

  // To be called by I2cBanjoChild.
  void Transact(const i2c_op_t* op_list, size_t op_count, i2c_transact_callback callback,
                void* cookie);

  zx_status_t GetMaxTransferSize(size_t* out_size);

 private:
  static zx_status_t CreateAndAddDevices(zx_device_t* parent, uint16_t address, uint32_t bus_id,
                                         cpp20::span<const zx_device_prop_t> props,
                                         cpp20::span<const uint8_t> metadata,
                                         const fbl::RefPtr<I2cBus>& bus,
                                         async_dispatcher_t* dispatcher);

  fbl::RefPtr<I2cBus> bus_;
  const uint16_t address_;
};

class I2cFidlChild;
using I2cFidlChildType = ddk::Device<I2cFidlChild, ddk::Messageable<fidl_i2c::Device2>::Mixin>;

class I2cFidlChild : public I2cFidlChildType, public I2cChild {
 public:
  I2cFidlChild(zx_device_t* parent, fbl::RefPtr<I2cBus> bus, uint16_t address)
      : I2cFidlChildType(parent), I2cChild(std::move(bus), address) {}

  static zx_status_t CreateAndAddDevice(zx_device_t* parent, uint16_t address, uint32_t bus_id,
                                        cpp20::span<const zx_device_prop_t> props,
                                        cpp20::span<const uint8_t> metadata,
                                        const fbl::RefPtr<I2cBus>& bus,
                                        async_dispatcher_t* dispatcher);

  void DdkRelease() { delete this; }

  void Transfer(TransferRequestView request, TransferCompleter::Sync& completer) override {
    I2cChild::Transfer(request, completer);
  }

 private:
  void Bind(fidl::ServerEnd<fidl_i2c::Device2> request) {
    fidl::BindServer<fidl::WireServer<fidl_i2c::Device2>>(device_get_dispatcher(parent()),
                                                          std::move(request), this);
  }

  std::optional<svc::Outgoing> outgoing_dir_;
};

class I2cBanjoChild;
using I2cBanjoChildType = ddk::Device<I2cBanjoChild, ddk::Messageable<fidl_i2c::Device2>::Mixin>;

class I2cBanjoChild : public I2cBanjoChildType,
                      public ddk::I2cProtocol<I2cBanjoChild, ddk::base_protocol>,
                      public I2cChild {
 public:
  I2cBanjoChild(zx_device_t* parent, fbl::RefPtr<I2cBus> bus, uint16_t address)
      : I2cBanjoChildType(parent), I2cChild(std::move(bus), address) {}

  static zx_status_t CreateAndAddDevice(zx_device_t* parent, uint16_t address, uint32_t bus_id,
                                        cpp20::span<const zx_device_prop_t> props,
                                        cpp20::span<const uint8_t> metadata,
                                        const fbl::RefPtr<I2cBus>& bus);

  void DdkRelease() { delete this; }

  void Transfer(TransferRequestView request, TransferCompleter::Sync& completer) override {
    I2cChild::Transfer(request, completer);
  }

  void I2cTransact(const i2c_op_t* op_list, size_t op_count, i2c_transact_callback callback,
                   void* cookie) {
    Transact(op_list, op_count, callback, cookie);
  }

  zx_status_t I2cGetMaxTransferSize(size_t* out_size) { return GetMaxTransferSize(out_size); }
};

}  // namespace i2c

#endif  // SRC_DEVICES_I2C_DRIVERS_I2C_I2C_CHILD_H_

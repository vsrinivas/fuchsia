// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TEMPERATURE_DRIVERS_SHTV3_SHTV3_H_
#define SRC_DEVICES_TEMPERATURE_DRIVERS_SHTV3_SHTV3_H_

#include <fuchsia/hardware/temperature/llcpp/fidl.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/zx/status.h>

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

namespace temperature {

class Shtv3Device;
using DeviceType = ddk::Device<Shtv3Device, ddk::Messageable>;
namespace temperature_fidl = llcpp::fuchsia::hardware::temperature;

class Shtv3Device : public DeviceType,
                    public temperature_fidl::Device::Interface,
                    public ddk::EmptyProtocol<ZX_PROTOCOL_TEMPERATURE> {
 public:
  Shtv3Device(zx_device_t* parent, ddk::I2cChannel i2c) : DeviceType(parent), i2c_(i2c) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease();

  void GetTemperatureCelsius(GetTemperatureCelsiusCompleter::Sync& completer) override;

  // Visible for testing.
  zx_status_t Init();
  zx::status<float> ReadTemperature();

 private:
  zx::status<uint16_t> Read16();
  zx_status_t Write16(uint16_t value);

  ddk::I2cChannel i2c_;
};

}  // namespace temperature

#endif  // SRC_DEVICES_TEMPERATURE_DRIVERS_SHTV3_SHTV3_H_

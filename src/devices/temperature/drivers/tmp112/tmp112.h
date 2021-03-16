// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TEMPERATURE_DRIVERS_TMP112_TMP112_H_
#define SRC_DEVICES_TEMPERATURE_DRIVERS_TMP112_TMP112_H_

#include <fuchsia/hardware/temperature/llcpp/fidl.h>
#include <lib/ddk/device.h>
#include <lib/device-protocol/i2c-channel.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

namespace temperature {

#define LOG_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define LOG_SPEW(fmt, ...) zxlogf(TRACE, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define LOG_TRACE zxlogf(INFO, "[%s %d]", __func__, __LINE__)

enum : uint8_t {
  kTemperatureReg = 0x00,
  kConfigReg = 0x01,
  kTLowReg = 0x02,
  kTHighReg = 0x03,
};

constexpr uint16_t kConfigExtendedMode = 1 << 12;
constexpr uint16_t kConfigOneShotMode = 1 << 7;
constexpr uint16_t kConfigConversionResolutionMask = 3 << 5;
constexpr uint16_t kConfigConvertResolutionSet12Bit = 3 << 5;

constexpr uint16_t kTemperatureExtendedModeSet = 1 << 0;
constexpr uint16_t kTemperatureExtendedModeShift = 3;
constexpr uint16_t kTemperatureNormalModeShift = 4;
constexpr float kTemperatureResolution = 0.0625;

class Tmp112Device;
using DdkDeviceType = ddk::Device<Tmp112Device, ddk::Unbindable, ddk::Messageable>;
namespace temperature_fidl = fuchsia_hardware_temperature;

class Tmp112Device : public DdkDeviceType,
                     public temperature_fidl::Device::Interface,
                     public ddk::EmptyProtocol<ZX_PROTOCOL_TEMPERATURE> {
 public:
  Tmp112Device(zx_device_t* parent, ddk::I2cChannel i2c)
      : DdkDeviceType(parent), i2c_(std::move(i2c)) {}

  static zx_status_t Bind(void* ctx, zx_device_t* parent);
  zx_status_t Init();
  float RegToTemperatureCelsius(uint16_t reg);

  // Ddk Hooks
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);

  // FIDL calls
  void GetTemperatureCelsius(GetTemperatureCelsiusCompleter::Sync& completer) override;

 private:
  zx_status_t ReadReg(uint8_t addr, uint16_t* val);
  zx_status_t WriteReg(uint8_t addr, uint16_t val);

  ddk::I2cChannel i2c_;
  uint16_t config_data_ = 0;
};

}  // namespace temperature

#endif  // SRC_DEVICES_TEMPERATURE_DRIVERS_TMP112_TMP112_H_

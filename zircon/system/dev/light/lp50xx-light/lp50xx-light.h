// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/hardware/light/llcpp/fidl.h>
#include <lib/device-protocol/i2c.h>
#include <threads.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/i2c.h>
#include <fbl/array.h>

namespace lp50xx_light {

class Lp50xxLight;
using Lp50xxLightType = ddk::Device<Lp50xxLight, ddk::Messageable>;

class Lp50xxLight : public Lp50xxLightType,
                    public llcpp::fuchsia::hardware::light::Light::Interface,
                    public ddk::EmptyProtocol<ZX_PROTOCOL_LIGHT> {
 public:
  explicit Lp50xxLight(zx_device_t* parent) : Lp50xxLightType(parent) {}

  Lp50xxLight(const Lp50xxLight&) = delete;
  Lp50xxLight(Lp50xxLight&&) = delete;
  Lp50xxLight& operator=(const Lp50xxLight&) = delete;
  Lp50xxLight& operator=(Lp50xxLight&&) = delete;

  virtual ~Lp50xxLight() = default;

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease();

  // FIDL messages.
  void GetName(uint32_t index, GetNameCompleter::Sync completer) override;
  void GetCount(GetCountCompleter::Sync completer) override;
  void HasCapability(uint32_t index, llcpp::fuchsia::hardware::light::Capability capability,
                     HasCapabilityCompleter::Sync completer) override;
  void GetSimpleValue(uint32_t index, GetSimpleValueCompleter::Sync completer) override;
  void SetSimpleValue(uint32_t index, uint8_t value,
                      SetSimpleValueCompleter::Sync completer) override;
  void GetRgbValue(uint32_t index, GetRgbValueCompleter::Sync completer) override;
  void SetRgbValue(uint32_t index, llcpp::fuchsia::hardware::light::Rgb value,
                   SetRgbValueCompleter::Sync completer) override;

  bool BlinkTest();
  zx_status_t Init();

 protected:
  // virtual method overloaded in unit test
  virtual zx_status_t InitHelper();

  zx_status_t SetRgbValue(uint32_t index, llcpp::fuchsia::hardware::light::Rgb rgb);
  zx_status_t GetRgbValue(uint32_t index, llcpp::fuchsia::hardware::light::Rgb* rgb);

  uint32_t pid_ = 0;
  ddk::I2cProtocolClient i2c_;

 private:
  static constexpr uint32_t kComponentCount = 2;
  static constexpr uint32_t kPdevComponent = 0;
  static constexpr uint32_t kI2cComponent = 1;

  zx_status_t Lp50xxRegConfig();

  static constexpr size_t kNameLength = ZX_MAX_NAME_LEN;
  fbl::Array<char[kNameLength]> names_;
  uint32_t led_count_ = 0;
  uint32_t led_color_addr_ = 0;
  uint32_t reset_addr_ = 0;
};

}  // namespace lp50xx_light

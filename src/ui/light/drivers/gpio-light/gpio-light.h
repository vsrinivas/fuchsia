// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_LIGHT_GPIO_LIGHT_GPIO_LIGHT_H_
#define ZIRCON_SYSTEM_DEV_LIGHT_GPIO_LIGHT_GPIO_LIGHT_H_

#include <fuchsia/hardware/light/llcpp/fidl.h>
#include <threads.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/gpio.h>
#include <fbl/array.h>

namespace gpio_light {

class GpioLight;
using GpioLightType = ddk::Device<GpioLight, ddk::Messageable>;

class GpioLight : public GpioLightType,
                  public llcpp::fuchsia::hardware::light::Light::Interface,
                  public ddk::EmptyProtocol<ZX_PROTOCOL_LIGHT> {
 public:
  explicit GpioLight(zx_device_t* parent) : GpioLightType(parent) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease();

  // FIDL messages.
  void GetName(uint32_t index, GetNameCompleter::Sync completer);
  void GetCount(GetCountCompleter::Sync completer);
  void HasCapability(uint32_t index, llcpp::fuchsia::hardware::light::Capability capability,
                     HasCapabilityCompleter::Sync completer);
  void GetSimpleValue(uint32_t index, GetSimpleValueCompleter::Sync completer);
  void SetSimpleValue(uint32_t index, uint8_t value, SetSimpleValueCompleter::Sync completer);
  void GetRgbValue(uint32_t index, GetRgbValueCompleter::Sync completer);
  void SetRgbValue(uint32_t index, llcpp::fuchsia::hardware::light::Rgb value,
                   SetRgbValueCompleter::Sync completer);

  void GetGroupInfo(uint32_t group_id, GetGroupInfoCompleter::Sync completer) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, nullptr);
  }
  void GetGroupCurrentSimpleValue(uint32_t group_id,
                                  GetGroupCurrentSimpleValueCompleter::Sync completer) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, ::fidl::VectorView<bool>(nullptr, false));
  }
  void SetGroupSimpleValue(uint32_t group_id, ::fidl::VectorView<bool> values,
                           SetGroupSimpleValueCompleter::Sync completer) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED);
  }
  void GetGroupCurrentBrightnessValue(uint32_t group_id,
                                      GetGroupCurrentBrightnessValueCompleter::Sync completer) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, ::fidl::VectorView<uint8_t>(nullptr, 0));
  }
  void SetGroupBrightnessValue(uint32_t group_id, ::fidl::VectorView<uint8_t> values,
                               SetGroupBrightnessValueCompleter::Sync completer) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED);
  }
  void GetGroupCurrentRgbValue(uint32_t group_id,
                               GetGroupCurrentRgbValueCompleter::Sync completer) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED,
                    ::fidl::VectorView<::llcpp::fuchsia::hardware::light::Rgb>(nullptr, 0));
  }
  void SetGroupRgbValue(uint32_t group_id,
                        ::fidl::VectorView<llcpp::fuchsia::hardware::light::Rgb> values,
                        SetGroupRgbValueCompleter::Sync completer) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED);
  }

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(GpioLight);

  zx_status_t Init();

  static constexpr size_t kNameLength = ZX_MAX_NAME_LEN;

  fbl::Array<ddk::GpioProtocolClient> gpios_;
  fbl::Array<char> names_;
  uint32_t gpio_count_;
};

}  // namespace gpio_light

#endif  // ZIRCON_SYSTEM_DEV_LIGHT_GPIO_LIGHT_GPIO_LIGHT_H_

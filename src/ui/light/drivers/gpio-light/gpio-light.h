// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIGHT_DRIVERS_GPIO_LIGHT_GPIO_LIGHT_H_
#define SRC_UI_LIGHT_DRIVERS_GPIO_LIGHT_GPIO_LIGHT_H_

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
  void GetNumLights(GetNumLightsCompleter::Sync completer);
  void GetNumLightGroups(GetNumLightGroupsCompleter::Sync completer);
  void GetInfo(uint32_t index, GetInfoCompleter::Sync completer);
  void GetCurrentSimpleValue(uint32_t index, GetCurrentSimpleValueCompleter::Sync completer);
  void SetSimpleValue(uint32_t index, bool value, SetSimpleValueCompleter::Sync completer);
  void GetCurrentBrightnessValue(uint32_t index,
                                 GetCurrentBrightnessValueCompleter::Sync completer);
  void SetBrightnessValue(uint32_t index, uint8_t value,
                          SetBrightnessValueCompleter::Sync completer);
  void GetCurrentRgbValue(uint32_t index, GetCurrentRgbValueCompleter::Sync completer);
  void SetRgbValue(uint32_t index, llcpp::fuchsia::hardware::light::Rgb value,
                   SetRgbValueCompleter::Sync completer);

  void GetGroupInfo(uint32_t group_id, GetGroupInfoCompleter::Sync completer) {
    completer.ReplyError(llcpp::fuchsia::hardware::light::LightError::NOT_SUPPORTED);
  }
  void GetGroupCurrentSimpleValue(uint32_t group_id,
                                  GetGroupCurrentSimpleValueCompleter::Sync completer) {
    completer.ReplyError(llcpp::fuchsia::hardware::light::LightError::NOT_SUPPORTED);
  }
  void SetGroupSimpleValue(uint32_t group_id, ::fidl::VectorView<bool> values,
                           SetGroupSimpleValueCompleter::Sync completer) {
    completer.ReplyError(llcpp::fuchsia::hardware::light::LightError::NOT_SUPPORTED);
  }
  void GetGroupCurrentBrightnessValue(uint32_t group_id,
                                      GetGroupCurrentBrightnessValueCompleter::Sync completer) {
    completer.ReplyError(llcpp::fuchsia::hardware::light::LightError::NOT_SUPPORTED);
  }
  void SetGroupBrightnessValue(uint32_t group_id, ::fidl::VectorView<uint8_t> values,
                               SetGroupBrightnessValueCompleter::Sync completer) {
    completer.ReplyError(llcpp::fuchsia::hardware::light::LightError::NOT_SUPPORTED);
  }
  void GetGroupCurrentRgbValue(uint32_t group_id,
                               GetGroupCurrentRgbValueCompleter::Sync completer) {
    completer.ReplyError(llcpp::fuchsia::hardware::light::LightError::NOT_SUPPORTED);
  }
  void SetGroupRgbValue(uint32_t group_id,
                        ::fidl::VectorView<llcpp::fuchsia::hardware::light::Rgb> values,
                        SetGroupRgbValueCompleter::Sync completer) {
    completer.ReplyError(llcpp::fuchsia::hardware::light::LightError::NOT_SUPPORTED);
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

#endif  // SRC_UI_LIGHT_DRIVERS_GPIO_LIGHT_GPIO_LIGHT_H_

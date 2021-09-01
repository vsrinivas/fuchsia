// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIGHT_DRIVERS_GPIO_LIGHT_GPIO_LIGHT_H_
#define SRC_UI_LIGHT_DRIVERS_GPIO_LIGHT_GPIO_LIGHT_H_

#include <fidl/fuchsia.hardware.light/cpp/wire.h>
#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <threads.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/array.h>

namespace gpio_light {

class GpioLight;
using GpioLightType =
    ddk::Device<GpioLight, ddk::Messageable<fuchsia_hardware_light::Light>::Mixin>;

class GpioLight : public GpioLightType, public ddk::EmptyProtocol<ZX_PROTOCOL_LIGHT> {
 public:
  explicit GpioLight(zx_device_t* parent) : GpioLightType(parent) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkRelease();

  // FIDL messages.
  void GetNumLights(GetNumLightsRequestView request,
                    GetNumLightsCompleter::Sync& completer) override;
  void GetNumLightGroups(GetNumLightGroupsRequestView request,
                         GetNumLightGroupsCompleter::Sync& completer) override;
  void GetInfo(GetInfoRequestView request, GetInfoCompleter::Sync& completer) override;
  void GetCurrentSimpleValue(GetCurrentSimpleValueRequestView request,
                             GetCurrentSimpleValueCompleter::Sync& completer) override;
  void SetSimpleValue(SetSimpleValueRequestView request,
                      SetSimpleValueCompleter::Sync& completer) override;
  void GetCurrentBrightnessValue(GetCurrentBrightnessValueRequestView request,
                                 GetCurrentBrightnessValueCompleter::Sync& completer) override;
  void SetBrightnessValue(SetBrightnessValueRequestView request,
                          SetBrightnessValueCompleter::Sync& completer) override;
  void GetCurrentRgbValue(GetCurrentRgbValueRequestView request,
                          GetCurrentRgbValueCompleter::Sync& completer) override;
  void SetRgbValue(SetRgbValueRequestView request, SetRgbValueCompleter::Sync& completer) override;

  void GetGroupInfo(GetGroupInfoRequestView request,
                    GetGroupInfoCompleter::Sync& completer) override {
    completer.ReplyError(fuchsia_hardware_light::wire::LightError::kNotSupported);
  }
  void GetGroupCurrentSimpleValue(GetGroupCurrentSimpleValueRequestView request,
                                  GetGroupCurrentSimpleValueCompleter::Sync& completer) override {
    completer.ReplyError(fuchsia_hardware_light::wire::LightError::kNotSupported);
  }
  void SetGroupSimpleValue(SetGroupSimpleValueRequestView request,
                           SetGroupSimpleValueCompleter::Sync& completer) override {
    completer.ReplyError(fuchsia_hardware_light::wire::LightError::kNotSupported);
  }
  void GetGroupCurrentBrightnessValue(
      GetGroupCurrentBrightnessValueRequestView request,
      GetGroupCurrentBrightnessValueCompleter::Sync& completer) override {
    completer.ReplyError(fuchsia_hardware_light::wire::LightError::kNotSupported);
  }
  void SetGroupBrightnessValue(SetGroupBrightnessValueRequestView request,
                               SetGroupBrightnessValueCompleter::Sync& completer) override {
    completer.ReplyError(fuchsia_hardware_light::wire::LightError::kNotSupported);
  }
  void GetGroupCurrentRgbValue(GetGroupCurrentRgbValueRequestView request,
                               GetGroupCurrentRgbValueCompleter::Sync& completer) override {
    completer.ReplyError(fuchsia_hardware_light::wire::LightError::kNotSupported);
  }
  void SetGroupRgbValue(SetGroupRgbValueRequestView request,
                        SetGroupRgbValueCompleter::Sync& completer) override {
    completer.ReplyError(fuchsia_hardware_light::wire::LightError::kNotSupported);
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

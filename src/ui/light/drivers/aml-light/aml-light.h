// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIGHT_DRIVERS_AML_LIGHT_AML_LIGHT_H_
#define SRC_UI_LIGHT_DRIVERS_AML_LIGHT_AML_LIGHT_H_

#include <fidl/fuchsia.hardware.light/cpp/wire.h>
#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <fuchsia/hardware/pwm/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <threads.h>

#include <optional>
#include <string>
#include <vector>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/array.h>
#include <soc/aml-common/aml-pwm-regs.h>

namespace aml_light {

using fuchsia_hardware_light::Light;
using fuchsia_hardware_light::wire::Capability;
using fuchsia_hardware_light::wire::LightError;
using fuchsia_hardware_light::wire::Rgb;

class AmlLight;
using AmlLightType = ddk::Device<AmlLight, ddk::Messageable<Light>::Mixin>;

class LightDevice {
 public:
  LightDevice(std::string name, ddk::GpioProtocolClient gpio,
              std::optional<ddk::PwmProtocolClient> pwm)
      : name_(std::move(name)), gpio_(gpio), pwm_(pwm) {}

  zx_status_t Init(bool init_on);

  const std::string GetName() const { return name_; }
  Capability GetCapability() const {
    return pwm_.has_value() ? Capability::kBrightness : Capability::kSimple;
  }
  bool GetCurrentSimpleValue() const { return (value_ != 0); }
  zx_status_t SetSimpleValue(bool value);
  double GetCurrentBrightnessValue() const { return value_; }
  zx_status_t SetBrightnessValue(double value);

 private:
  std::string name_;
  ddk::GpioProtocolClient gpio_;
  std::optional<ddk::PwmProtocolClient> pwm_;

  double value_ = 0;
};

class AmlLight : public AmlLightType, public ddk::EmptyProtocol<ZX_PROTOCOL_LIGHT> {
 public:
  explicit AmlLight(zx_device_t* parent) : AmlLightType(parent) {}

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
    completer.ReplyError(LightError::kNotSupported);
  }
  void GetGroupCurrentSimpleValue(GetGroupCurrentSimpleValueRequestView request,
                                  GetGroupCurrentSimpleValueCompleter::Sync& completer) override {
    completer.ReplyError(LightError::kNotSupported);
  }
  void SetGroupSimpleValue(SetGroupSimpleValueRequestView request,
                           SetGroupSimpleValueCompleter::Sync& completer) override {
    completer.ReplyError(LightError::kNotSupported);
  }
  void GetGroupCurrentBrightnessValue(
      GetGroupCurrentBrightnessValueRequestView request,
      GetGroupCurrentBrightnessValueCompleter::Sync& completer) override {
    completer.ReplyError(LightError::kNotSupported);
  }
  void SetGroupBrightnessValue(SetGroupBrightnessValueRequestView request,
                               SetGroupBrightnessValueCompleter::Sync& completer) override {
    completer.ReplyError(LightError::kNotSupported);
  }
  void GetGroupCurrentRgbValue(GetGroupCurrentRgbValueRequestView request,
                               GetGroupCurrentRgbValueCompleter::Sync& completer) override {
    completer.ReplyError(LightError::kNotSupported);
  }
  void SetGroupRgbValue(SetGroupRgbValueRequestView request,
                        SetGroupRgbValueCompleter::Sync& completer) override {
    completer.ReplyError(LightError::kNotSupported);
  }

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(AmlLight);
  friend class FakeAmlLight;

  zx_status_t Init();

  static constexpr size_t kNameLength = ZX_MAX_NAME_LEN;

  std::vector<LightDevice> lights_;
};

}  // namespace aml_light

#endif  // SRC_UI_LIGHT_DRIVERS_AML_LIGHT_AML_LIGHT_H_

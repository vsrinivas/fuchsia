// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIGHT_DRIVERS_LP50XX_LIGHT_LP50XX_LIGHT_H_
#define SRC_UI_LIGHT_DRIVERS_LP50XX_LIGHT_LP50XX_LIGHT_H_

#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <fidl/fuchsia.hardware.light/cpp/wire.h>
#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <threads.h>

#include <map>
#include <vector>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/array.h>

namespace lp50xx_light {

class Lp50xxLight;
using Lp50xxLightType =
    ddk::Device<Lp50xxLight, ddk::Messageable<fuchsia_hardware_light::Light>::Mixin>;

class Lp50xxLight : public Lp50xxLightType, public ddk::EmptyProtocol<ZX_PROTOCOL_LIGHT> {
 public:
  explicit Lp50xxLight(zx_device_t* parent) : Lp50xxLightType(parent) {}

  Lp50xxLight(const Lp50xxLight&) = delete;
  Lp50xxLight(Lp50xxLight&&) = delete;
  Lp50xxLight& operator=(const Lp50xxLight&) = delete;
  Lp50xxLight& operator=(Lp50xxLight&&) = delete;

  virtual ~Lp50xxLight() = default;

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkRelease();

  // FIDL messages.
  void GetNumLights(GetNumLightsCompleter::Sync& completer) override;
  void GetNumLightGroups(GetNumLightGroupsCompleter::Sync& completer) override;
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
                    GetGroupInfoCompleter::Sync& completer) override;
  void GetGroupCurrentSimpleValue(GetGroupCurrentSimpleValueRequestView request,
                                  GetGroupCurrentSimpleValueCompleter::Sync& completer) override;
  void SetGroupSimpleValue(SetGroupSimpleValueRequestView request,
                           SetGroupSimpleValueCompleter::Sync& completer) override;
  void GetGroupCurrentBrightnessValue(
      GetGroupCurrentBrightnessValueRequestView request,
      GetGroupCurrentBrightnessValueCompleter::Sync& completer) override;
  void SetGroupBrightnessValue(SetGroupBrightnessValueRequestView request,
                               SetGroupBrightnessValueCompleter::Sync& completer) override;
  void GetGroupCurrentRgbValue(GetGroupCurrentRgbValueRequestView request,
                               GetGroupCurrentRgbValueCompleter::Sync& completer) override;
  void SetGroupRgbValue(SetGroupRgbValueRequestView request,
                        SetGroupRgbValueCompleter::Sync& completer) override;

  bool BlinkTest();
  zx_status_t Init();

 protected:
  // virtual method overloaded in unit test
  virtual zx_status_t InitHelper();

  zx_status_t SetRgbValue(uint32_t index, fuchsia_hardware_light::wire::Rgb rgb);
  zx_status_t GetRgbValue(uint32_t index, fuchsia_hardware_light::wire::Rgb* rgb);

  zx_status_t SetBrightness(uint32_t index, double brightness);
  zx_status_t GetBrightness(uint32_t index, double* brightness);

  uint32_t pid_ = 0;
  fidl::ClientEnd<fuchsia_hardware_i2c::Device> i2c_;

 private:
  friend class Lp50xxLightTest;

  zx_status_t Lp50xxRegConfig();

  static constexpr size_t kNameLength = ZX_MAX_NAME_LEN;
  fbl::Array<char[kNameLength]> names_;
  uint32_t led_count_ = 0;
  uint32_t led_color_addr_ = 0;
  uint32_t reset_addr_ = 0;
  uint32_t brightness_addr_ = 0;
  fbl::Array<char> group_names_;
  std::map<uint32_t, std::vector<uint32_t>> group2led_;
};

}  // namespace lp50xx_light

#endif  // SRC_UI_LIGHT_DRIVERS_LP50XX_LIGHT_LP50XX_LIGHT_H_

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIGHT_DRIVERS_LP50XX_LIGHT_LP50XX_LIGHT_H_
#define SRC_UI_LIGHT_DRIVERS_LP50XX_LIGHT_LP50XX_LIGHT_H_

#include <fuchsia/hardware/light/llcpp/fidl.h>
#include <lib/device-protocol/i2c.h>
#include <threads.h>

#include <map>

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
  void GetNumLights(GetNumLightsCompleter::Sync completer) override;
  void GetNumLightGroups(GetNumLightGroupsCompleter::Sync completer) override;
  void GetInfo(uint32_t index, GetInfoCompleter::Sync completer) override;
  void GetCurrentSimpleValue(uint32_t index,
                             GetCurrentSimpleValueCompleter::Sync completer) override;
  void SetSimpleValue(uint32_t index, bool value, SetSimpleValueCompleter::Sync completer) override;
  void GetCurrentBrightnessValue(uint32_t index,
                                 GetCurrentBrightnessValueCompleter::Sync completer) override;
  void SetBrightnessValue(uint32_t index, double value,
                          SetBrightnessValueCompleter::Sync completer) override;
  void GetCurrentRgbValue(uint32_t index, GetCurrentRgbValueCompleter::Sync completer) override;
  void SetRgbValue(uint32_t index, llcpp::fuchsia::hardware::light::Rgb value,
                   SetRgbValueCompleter::Sync completer) override;

  void GetGroupInfo(uint32_t group_id, GetGroupInfoCompleter::Sync completer) override;
  void GetGroupCurrentSimpleValue(uint32_t group_id,
                                  GetGroupCurrentSimpleValueCompleter::Sync completer) override;
  void SetGroupSimpleValue(uint32_t group_id, ::fidl::VectorView<bool> values,
                           SetGroupSimpleValueCompleter::Sync completer) override;
  void GetGroupCurrentBrightnessValue(
      uint32_t group_id, GetGroupCurrentBrightnessValueCompleter::Sync completer) override;
  void SetGroupBrightnessValue(uint32_t group_id, ::fidl::VectorView<double> values,
                               SetGroupBrightnessValueCompleter::Sync completer) override;
  void GetGroupCurrentRgbValue(uint32_t group_id,
                               GetGroupCurrentRgbValueCompleter::Sync completer) override;
  void SetGroupRgbValue(uint32_t group_id,
                        ::fidl::VectorView<llcpp::fuchsia::hardware::light::Rgb> values,
                        SetGroupRgbValueCompleter::Sync completer) override;

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
  friend class Lp50xxLightTest;

  static constexpr uint32_t kFragmentCount = 2;
  static constexpr uint32_t kPdevFragment = 0;
  static constexpr uint32_t kI2cFragment = 1;

  zx_status_t Lp50xxRegConfig();

  static constexpr size_t kNameLength = ZX_MAX_NAME_LEN;
  fbl::Array<char[kNameLength]> names_;
  uint32_t led_count_ = 0;
  uint32_t led_color_addr_ = 0;
  uint32_t reset_addr_ = 0;
  fbl::Array<char> group_names_;
  std::map<uint32_t, std::vector<uint32_t>> group2led_;
};

}  // namespace lp50xx_light

#endif  // SRC_UI_LIGHT_DRIVERS_LP50XX_LIGHT_LP50XX_LIGHT_H_

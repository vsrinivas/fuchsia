// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lp50xx-light.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/lights.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/platform/device.h>
#include <fbl/alloc_checker.h>

#include "lp50xx-regs.h"

namespace lp50xx_light {

static bool run_blink_test(void* ctx, zx_device_t* parent, zx_handle_t channel) {
  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<Lp50xxLight>(new (&ac) Lp50xxLight(parent));
  if (!ac.check()) {
    return false;
  }
  auto status = dev->Init();
  if (status != ZX_OK) {
    return false;
  }
  return dev->BlinkTest();
}

bool Lp50xxLight::BlinkTest() {
  zx_status_t status;
  llcpp::fuchsia::hardware::light::Rgb rgb = {};

  for (uint32_t led = 0; led < led_count_; led++) {
    // incrementing color in steps of 16 to reduce time taken for the test
    for (uint32_t red = 0; red <= 0xff; red += 16) {
      for (uint32_t green = 0; green <= 0xff; green += 16) {
        for (uint32_t blue = 0; blue <= 0xff; blue += 16) {
          rgb.red = static_cast<uint8_t>(red);
          rgb.blue = static_cast<uint8_t>(blue);
          rgb.green = static_cast<uint8_t>(green);
          status = SetRgbValue(led, rgb);
          if (status != ZX_OK) {
            zxlogf(ERROR, "%s: Failed to set color R:%d G:%d B:%d", __func__, red, green, blue);
          }
          status = GetRgbValue(led, &rgb);
          if (status != ZX_OK) {
            zxlogf(ERROR, "%s: Failed to get color R:%d G:%d B:%d", __func__, red, green, blue);
          }
        }
      }
    }
  }

  for (uint32_t i = 0; i < led_count_; i++) {
    rgb.red = 0;
    rgb.green = 0;
    rgb.blue = 0;
    status = SetRgbValue(i, rgb);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to reset color", __PRETTY_FUNCTION__);
    }
  }

  zxlogf(INFO, "Lp50xxLight Blink test complete");
  return ZX_OK;
}

zx_status_t Lp50xxLight::Lp50xxRegConfig() {
  uint32_t led_count = 0;
  switch (pid_) {
    case PDEV_PID_TI_LP5018:
      led_count = 6;
      led_color_addr_ = 0x0f;
      reset_addr_ = 0x27;
      break;
    case PDEV_PID_TI_LP5024:
      led_count = 8;
      led_color_addr_ = 0x0f;
      reset_addr_ = 0x27;
      break;
    case PDEV_PID_TI_LP5030:
      led_count = 10;
      led_color_addr_ = 0x14;
      reset_addr_ = 0x38;
      break;
    case PDEV_PID_TI_LP5036:
      led_count = 12;
      led_color_addr_ = 0x14;
      reset_addr_ = 0x38;
      break;
    default:
      zxlogf(ERROR, "%s: unsupported PID %u", __func__, pid_);
      return ZX_ERR_NOT_SUPPORTED;
  }
  if (led_count != led_count_) {
    zxlogf(ERROR, "%s: incorrect number of LEDs %u != %u", __func__, led_count_, led_count);
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t Lp50xxLight::SetRgbValue(uint32_t index, llcpp::fuchsia::hardware::light::Rgb rgb) {
  auto status = RedColorReg::Get(led_color_addr_, index).FromValue(rgb.red).WriteTo(i2c_);
  if (status != ZX_OK) {
    return status;
  }

  status = GreenColorReg::Get(led_color_addr_, index).FromValue(rgb.green).WriteTo(i2c_);
  if (status != ZX_OK) {
    return status;
  }

  status = BlueColorReg::Get(led_color_addr_, index).FromValue(rgb.blue).WriteTo(i2c_);
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t Lp50xxLight::GetRgbValue(uint32_t index, llcpp::fuchsia::hardware::light::Rgb* rgb) {
  auto red = RedColorReg::Get(led_color_addr_, index).FromValue(0);
  auto green = GreenColorReg::Get(led_color_addr_, index).FromValue(0);
  auto blue = BlueColorReg::Get(led_color_addr_, index).FromValue(0);

  if (red.ReadFrom(i2c_) || green.ReadFrom(i2c_) || blue.ReadFrom(i2c_)) {
    zxlogf(ERROR, "Failed to read I2C color registers");
    return ZX_ERR_INTERNAL;
  }

  rgb->red = red.reg_value();
  rgb->green = green.reg_value();
  rgb->blue = blue.reg_value();

  return ZX_OK;
}

void Lp50xxLight::GetNumLights(GetNumLightsCompleter::Sync completer) {
  completer.Reply(led_count_);
}

void Lp50xxLight::GetNumLightGroups(GetNumLightGroupsCompleter::Sync completer) {
  completer.Reply(static_cast<uint32_t>(group_names_.size()));
}

void Lp50xxLight::GetInfo(uint32_t index, GetInfoCompleter::Sync completer) {
  if (index >= led_count_) {
    completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::INVALID_INDEX);
    return;
  }

  char name[kNameLength];
  if (names_.size() > 0) {
    // TODO(puneetha): Currently names_ is not set from metadata. This code will not be executed.
    snprintf(name, sizeof(name), "%s\n", names_[index]);
  } else {
    // Return "lp50xx-led-X" if no metadata was provided.
    snprintf(name, sizeof(name), "lp50xx-led-%u\n", index);
  }

  completer.ReplySuccess({
      .name = ::fidl::StringView(name, strlen(name)),
      .capability = ::llcpp::fuchsia::hardware::light::Capability::RGB,
  });

  return;
}

void Lp50xxLight::GetCurrentSimpleValue(uint32_t index,
                                        GetCurrentSimpleValueCompleter::Sync completer) {
  completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::NOT_SUPPORTED);
}

void Lp50xxLight::SetSimpleValue(uint32_t index, bool value,
                                 SetSimpleValueCompleter::Sync completer) {
  completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::NOT_SUPPORTED);
}

void Lp50xxLight::GetCurrentBrightnessValue(uint32_t index,
                                            GetCurrentBrightnessValueCompleter::Sync completer) {
  completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::NOT_SUPPORTED);
}

void Lp50xxLight::SetBrightnessValue(uint32_t index, uint8_t value,
                                     SetBrightnessValueCompleter::Sync completer) {
  completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::NOT_SUPPORTED);
}

void Lp50xxLight::GetCurrentRgbValue(uint32_t index, GetCurrentRgbValueCompleter::Sync completer) {
  llcpp::fuchsia::hardware::light::Rgb rgb = {};
  if (index >= led_count_) {
    completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::INVALID_INDEX);
    return;
  }

  if (GetRgbValue(index, &rgb) != ZX_OK) {
    completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::FAILED);
  } else {
    completer.ReplySuccess(rgb);
  }
}

void Lp50xxLight::SetRgbValue(uint32_t index, llcpp::fuchsia::hardware::light::Rgb value,
                              SetRgbValueCompleter::Sync completer) {
  if (index >= led_count_) {
    completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::INVALID_INDEX);
    return;
  }

  if (SetRgbValue(index, value) != ZX_OK) {
    completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::FAILED);
  } else {
    completer.ReplySuccess();
  }
}

void Lp50xxLight::GetGroupInfo(uint32_t group_id, GetGroupInfoCompleter::Sync completer) {
  if (group_id >= group2led_.size()) {
    completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::INVALID_INDEX);
    return;
  }

  char name[kNameLength];
  if (group_names_.size() > 0) {
    snprintf(name, sizeof(name), "%s\n", &group_names_[group_id * kNameLength]);
  } else {
    // Return "led-group-X" if no metadata was provided.
    snprintf(name, sizeof(name), "led-group-%u\n", group_id);
  }

  completer.ReplySuccess({
      .name = ::fidl::StringView(name, strlen(name)),
      .count = static_cast<uint32_t>(group2led_[group_id].size()),
      .capability = ::llcpp::fuchsia::hardware::light::Capability::RGB,
  });
}

void Lp50xxLight::GetGroupCurrentSimpleValue(uint32_t group_id,
                                             GetGroupCurrentSimpleValueCompleter::Sync completer) {
  completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::NOT_SUPPORTED);
}

void Lp50xxLight::SetGroupSimpleValue(uint32_t group_id, ::fidl::VectorView<bool> values,
                                      SetGroupSimpleValueCompleter::Sync completer) {
  completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::NOT_SUPPORTED);
}

void Lp50xxLight::GetGroupCurrentBrightnessValue(
    uint32_t group_id, GetGroupCurrentBrightnessValueCompleter::Sync completer) {
  completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::NOT_SUPPORTED);
}

void Lp50xxLight::SetGroupBrightnessValue(uint32_t group_id, ::fidl::VectorView<uint8_t> values,
                                          SetGroupBrightnessValueCompleter::Sync completer) {
  completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::NOT_SUPPORTED);
}

void Lp50xxLight::GetGroupCurrentRgbValue(uint32_t group_id,
                                          GetGroupCurrentRgbValueCompleter::Sync completer) {
  ::fidl::VectorView<::llcpp::fuchsia::hardware::light::Rgb> empty(nullptr, 0);
  if (group_id >= group2led_.size()) {
    completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::INVALID_INDEX);
    return;
  }

  zx_status_t status = ZX_OK;
  std::vector<::llcpp::fuchsia::hardware::light::Rgb> out;
  for (auto led : group2led_[group_id]) {
    if (led >= led_count_) {
      completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::INVALID_INDEX);
      return;
    }

    llcpp::fuchsia::hardware::light::Rgb rgb = {};
    if ((status = GetRgbValue(led, &rgb)) != ZX_OK) {
      break;
    }
    out.emplace_back(rgb);
  }

  if (status != ZX_OK) {
    completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::FAILED);
  } else {
    completer.ReplySuccess(::fidl::VectorView<::llcpp::fuchsia::hardware::light::Rgb>(
        fidl::unowned_ptr(out.data()), out.size()));
  }
}

void Lp50xxLight::SetGroupRgbValue(uint32_t group_id,
                                   ::fidl::VectorView<llcpp::fuchsia::hardware::light::Rgb> values,
                                   SetGroupRgbValueCompleter::Sync completer) {
  if (group_id >= group2led_.size()) {
    completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::INVALID_INDEX);
    return;
  }

  zx_status_t status = ZX_OK;
  for (uint32_t i = 0; i < group2led_[group_id].size(); i++) {
    if (group2led_[group_id][i] >= led_count_) {
      completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::INVALID_INDEX);
      return;
    }

    if ((status = SetRgbValue(group2led_[group_id][i], values[i])) != ZX_OK) {
      break;
    }
  }
  if (status != ZX_OK) {
    completer.ReplyError(::llcpp::fuchsia::hardware::light::LightError::FAILED);
  } else {
    completer.ReplySuccess();
  }
}

zx_status_t Lp50xxLight::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  llcpp::fuchsia::hardware::light::Light::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void Lp50xxLight::DdkRelease() { delete this; }

zx_status_t Lp50xxLight::InitHelper() {
  // Get Pdev and I2C protocol.
  composite_protocol_t composite;
  auto status = device_get_protocol(parent(), ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Get ZX_PROTOCOL_COMPOSITE failed", __func__);
    return status;
  }

  zx_device_t* fragments[kFragmentCount];
  size_t actual;
  composite_get_fragments(&composite, fragments, countof(fragments), &actual);
  if (actual != kFragmentCount) {
    zxlogf(ERROR, "Invalid fragment count (need %d, have %zu)", kFragmentCount, actual);
    return ZX_ERR_INTERNAL;
  }

  // status = device_get_protocol(fragments[kI2cFragment], ZX_PROTOCOL_I2C, &i2c_);
  ddk::I2cProtocolClient i2c(fragments[kI2cFragment]);
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "ZX_PROTOCOL_I2C not found, err=%d", status);
    return status;
  }

  ddk::PDevProtocolClient pdev(fragments[kPdevFragment]);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: Get PBusProtocolClient failed", __func__);
    return ZX_ERR_INTERNAL;
  }

  pdev_device_info info = {};
  status = pdev.GetDeviceInfo(&info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GetDeviceInfo failed: %d", __func__, status);
    return ZX_ERR_INTERNAL;
  }
  pid_ = info.pid;
  i2c_ = std::move(i2c);

  fbl::AllocChecker ac;
  size_t metadata_size;
  if ((status = device_get_metadata_size(parent(), DEVICE_METADATA_LIGHTS, &metadata_size)) !=
      ZX_OK) {
    zxlogf(ERROR, "%s: couldn't get metadata size", __func__);
    return status;
  }
  auto configs =
      fbl::Array<lights_config_t>(new (&ac) lights_config_t[metadata_size], metadata_size);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  if ((status = device_get_metadata(parent(), DEVICE_METADATA_LIGHTS, configs.data(), metadata_size,
                                    &actual)) != ZX_OK) {
    return status;
  }
  if ((actual != metadata_size) || (actual % sizeof(lights_config_t) != 0)) {
    zxlogf(ERROR, "%s: wrong metadata size", __func__);
    return ZX_ERR_INVALID_ARGS;
  }
  led_count_ = static_cast<uint32_t>(metadata_size) / sizeof(lights_config_t);

  for (uint32_t i = 0; i < led_count_; i++) {
    group2led_[configs[i].group_id].push_back(i);
  }

  if ((status = device_get_metadata_size(parent(), DEVICE_METADATA_LIGHTS_GROUP_NAME,
                                         &metadata_size)) != ZX_OK) {
    zxlogf(ERROR, "%s: couldn't get metadata size", __func__);
    return status;
  }
  group_names_ = fbl::Array<char>(new (&ac) char[metadata_size], metadata_size);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  if ((status = device_get_metadata(parent(), DEVICE_METADATA_LIGHTS_GROUP_NAME,
                                    group_names_.data(), metadata_size, &actual)) != ZX_OK) {
    return status;
  }
  if ((actual != metadata_size) || (actual % kNameLength != 0) ||
      static_cast<uint32_t>(metadata_size / kNameLength) != group2led_.size()) {
    zxlogf(ERROR, "%s: wrong metadata size", __func__);
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

zx_status_t Lp50xxLight::Init() {
  InitHelper();

  // Set device specific register configuration
  zx_status_t status = Lp50xxRegConfig();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Device register configuration failed %d", __func__, status);
    return status;
  }

  // Enable device.
  auto dev_conf0 = DeviceConfig0Reg::Get().FromValue(0);
  dev_conf0.set_chip_enable(1);

  status = dev_conf0.WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Device enable failed %d", __func__, status);
    return status;
  }

  // Set Log_Scale_EN, Power_save_EN, Auto_incr_EN and PWm_Dithering_EN
  auto dev_conf1 = DeviceConfig1Reg::Get().FromValue(0);
  dev_conf1.set_log_scale_enable(1);
  dev_conf1.set_power_save_enable(1);
  dev_conf1.set_auto_incr_enable(1);
  dev_conf1.set_pwm_dithering_enable(1);
  status = dev_conf1.WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Device conf1 failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

zx_status_t Lp50xxLight::Create(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<Lp50xxLight>(new (&ac) Lp50xxLight(parent));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Init();
  if (status != ZX_OK) {
    return status;
  }

  status = dev->DdkAdd("lp50xx-light", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = dev.release();
  return ZX_OK;
}

static zx_driver_ops_t driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = nullptr,
    .bind = Lp50xxLight::Create,
    .create = nullptr,
    .release = nullptr,
    .run_unit_tests = run_blink_test,
};

}  // namespace lp50xx_light

// clang-format off
ZIRCON_DRIVER_BEGIN(lp50xx_light, lp50xx_light::driver_ops, "zircon", "0.1", 7)
  BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TI),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_TI_LED),
  BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_TI_LP5018),
  BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_TI_LP5024),
  BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_TI_LP5030),
  BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_TI_LP5036),
ZIRCON_DRIVER_END(lp50xx_light)
//clang-format on

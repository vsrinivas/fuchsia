// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/gpio.h>
#include <fbl/array.h>
#include <fuchsia/hardware/light/c/fidl.h>

#include <threads.h>

namespace gpio_light {

class GpioLight;
using GpioLightType = ddk::Device<GpioLight, ddk::Messageable>;

class GpioLight : public GpioLightType, public ddk::EmptyProtocol<ZX_PROTOCOL_LIGHT> {
 public:
  explicit GpioLight(zx_device_t* parent) : GpioLightType(parent) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease();

  // FIDL messages.
  zx_status_t MsgGetName(fidl_txn_t* txn, uint32_t index);
  zx_status_t MsgGetCount(fidl_txn_t* txn);
  zx_status_t MsgHasCapability(uint32_t index, fuchsia_hardware_light_Capability capability,
                               fidl_txn_t* txn);
  zx_status_t MsgGetSimpleValue(uint32_t index, fidl_txn_t* txn);
  zx_status_t MsgSetSimpleValue(uint32_t index, uint8_t value, fidl_txn_t* txn);
  zx_status_t MsgGetRgbValue(uint32_t index, fidl_txn_t* txn);
  zx_status_t MsgSetRgbValue(uint32_t index, const fuchsia_hardware_light_Rgb* value,
                             fidl_txn_t* txn);

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(GpioLight);

  zx_status_t Init();

  static constexpr size_t kNameLength = ZX_MAX_NAME_LEN;

  fbl::Array<ddk::GpioProtocolClient> gpios_;
  fbl::Array<char> names_;
  uint32_t gpio_count_;
};

}  // namespace gpio_light

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"

namespace bt_intel {

constexpr bluetooth::hci::OpCode kReadVersion = bluetooth::hci::VendorOpCode(0x0005);

struct IntelVersionReturnParams {
  bluetooth::hci::Status status;
  uint8_t hw_platform;
  uint8_t hw_variant;
  uint8_t hw_revision;
  uint8_t fw_variant;
  uint8_t fw_revision;
  uint8_t fw_build_num;
  uint8_t fw_build_week;
  uint8_t fw_build_year;
  uint8_t fw_patch_num;
} __PACKED;

constexpr bluetooth::hci::OpCode kReadBootParams = bluetooth::hci::VendorOpCode(0x000D);

struct IntelReadBootParamsReturnParams {
  bluetooth::hci::Status status;
  uint8_t otp_format;
  uint8_t otp_content;
  uint8_t otp_patch;
  uint16_t dev_revid;
  bluetooth::hci::GenericEnableParam secure_boot;
  uint8_t key_from_hdr;
  uint8_t key_type;
  bluetooth::hci::GenericEnableParam otp_lock;
  bluetooth::hci::GenericEnableParam api_lock;
  bluetooth::hci::GenericEnableParam debug_lock;
  bluetooth::common::DeviceAddressBytes otp_bdaddr;
  uint8_t min_fw_build_num;
  uint8_t min_fw_build_week;
  uint8_t min_fw_build_year;
  bluetooth::hci::GenericEnableParam limited_cce;
  uint8_t unlocked_state;
} __PACKED;

constexpr bluetooth::hci::OpCode kReset = bluetooth::hci::VendorOpCode(0x0001);

struct IntelResetCommandParams {
  uint8_t data[8];
} __PACKED;

}  // namespace bt_intel

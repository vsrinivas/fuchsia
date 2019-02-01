// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"

namespace bt_intel {

constexpr ::btlib::hci::OpCode kLoadPatch = ::btlib::hci::VendorOpCode(0x008e);

constexpr ::btlib::hci::OpCode kReadVersion =
    ::btlib::hci::VendorOpCode(0x0005);

struct IntelVersionReturnParams {
  ::btlib::hci::StatusCode status;
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

constexpr btlib::hci::OpCode kSecureSend = ::btlib::hci::VendorOpCode(0x0009);
constexpr ::btlib::hci::OpCode kReadBootParams =
    ::btlib::hci::VendorOpCode(0x000D);

struct IntelReadBootParamsReturnParams {
  ::btlib::hci::StatusCode status;
  uint8_t otp_format;
  uint8_t otp_content;
  uint8_t otp_patch;
  uint16_t dev_revid;
  ::btlib::hci::GenericEnableParam secure_boot;
  uint8_t key_from_hdr;
  uint8_t key_type;
  ::btlib::hci::GenericEnableParam otp_lock;
  ::btlib::hci::GenericEnableParam api_lock;
  ::btlib::hci::GenericEnableParam debug_lock;
  ::btlib::common::DeviceAddressBytes otp_bdaddr;
  uint8_t min_fw_build_num;
  uint8_t min_fw_build_week;
  uint8_t min_fw_build_year;
  ::btlib::hci::GenericEnableParam limited_cce;
  uint8_t unlocked_state;
} __PACKED;

constexpr ::btlib::hci::OpCode kReset = ::btlib::hci::VendorOpCode(0x0001);

struct IntelResetCommandParams {
  uint8_t data[8];
} __PACKED;

constexpr btlib::hci::OpCode kMfgModeChange = btlib::hci::VendorOpCode(0x0011);

enum class MfgDisableMode : uint8_t {
  kNoPatches = 0x00,
  kPatchesDisabled = 0x01,
  kPatchesEnabled = 0x02,
};

struct IntelMfgModeChangeCommandParams {
  btlib::hci::GenericEnableParam enable;
  MfgDisableMode disable_mode;
} __PACKED;

struct IntelSecureSendEventParams {
  uint8_t vendor_event_code;
  uint8_t result;
  uint16_t opcode;
  uint8_t status;
} __PACKED;

struct IntelBootloaderVendorEventParams {
  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(IntelBootloaderVendorEventParams);

  uint8_t vendor_event_code;
  uint8_t vendor_params[];
} __PACKED;

}  // namespace bt_intel

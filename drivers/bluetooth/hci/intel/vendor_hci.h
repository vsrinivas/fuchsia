// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/hci/control_packets.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"

#include <zx/channel.h>

namespace btintel {

constexpr btlib::hci::OpCode kReadVersion = btlib::hci::VendorOpCode(0x0005);

struct ReadVersionReturnParams {
  btlib::hci::Status status;
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

constexpr uint8_t kBootloaderFirmwareVariant = 0x06;
constexpr uint8_t kFirmwareFirmwareVariant = 0x23;

constexpr btlib::hci::OpCode kSecureSend = btlib::hci::VendorOpCode(0x0009);

constexpr btlib::hci::OpCode kReadBootParams = btlib::hci::VendorOpCode(0x000D);

struct ReadBootParamsReturnParams {
  btlib::hci::Status status;
  uint8_t otp_format;
  uint8_t otp_content;
  uint8_t otp_patch;
  uint16_t dev_revid;
  btlib::hci::GenericEnableParam secure_boot;
  uint8_t key_from_hdr;
  uint8_t key_type;
  btlib::hci::GenericEnableParam otp_lock;
  btlib::hci::GenericEnableParam api_lock;
  btlib::hci::GenericEnableParam debug_lock;
  btlib::common::DeviceAddressBytes otp_bdaddr;
  uint8_t min_fw_build_num;
  uint8_t min_fw_build_week;
  uint8_t min_fw_build_year;
  btlib::hci::GenericEnableParam limited_cce;
  uint8_t unlocked_state;
} __PACKED;

constexpr btlib::hci::OpCode kReset = btlib::hci::VendorOpCode(0x0001);

struct ResetCommandParams {
  uint8_t data[8];
} __PACKED;

constexpr btlib::hci::OpCode kMfgModeChange = btlib::hci::VendorOpCode(0x0011);

enum class MfgDisableMode : uint8_t {
  kNoPatches = 0x00,
  kPatchesDisabled = 0x01,
  kPatchesEnabled = 0x02,
};

struct MfgModeChangeCommandParams {
  btlib::hci::GenericEnableParam enable;
  MfgDisableMode disable_mode;
} __PACKED;

struct SecureSendEventParams {
  uint8_t vendor_event_code;
  uint8_t result;
  uint16_t opcode;
  uint8_t status;
} __PACKED;

struct BootloaderVendorEventParams {
  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(BootloaderVendorEventParams);

  uint8_t vendor_event_code;
  uint8_t vendor_params[];
} __PACKED;

class VendorHci {
 public:
  VendorHci(zx::channel* channel);

  ReadVersionReturnParams SendReadVersion() const;

  ReadBootParamsReturnParams SendReadBootParams() const;

  void SendReset() const;

  bool SendSecureSend(uint8_t type,
                      const btlib::common::BufferView& bytes) const;

  bool SendAndExpect(
      const btlib::common::PacketView<btlib::hci::CommandHeader>& command,
      std::deque<btlib::common::BufferView> events) const;

 private:
  // The channel to communicate on.
  zx::channel* channel_;

  void SendCommand(const btlib::common::PacketView<btlib::hci::CommandHeader>&
                       command) const;
  std::unique_ptr<btlib::hci::EventPacket> ReadEventPacket() const;
};

}  // namespace btintel

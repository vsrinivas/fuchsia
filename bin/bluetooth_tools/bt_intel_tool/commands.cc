// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "commands.h"

#include <endian.h>

#include <cstring>
#include <iostream>

#include "garnet/drivers/bluetooth/lib/common/manufacturer_names.h"
#include "garnet/drivers/bluetooth/lib/gap/advertising_data.h"
#include "garnet/drivers/bluetooth/lib/hci/advertising_report_parser.h"
#include "garnet/drivers/bluetooth/lib/hci/command_channel.h"
#include "garnet/drivers/bluetooth/lib/hci/control_packets.h"
#include "lib/fxl/strings/join_strings.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/time/time_delta.h"

#include "bt_intel.h"

using namespace bluetooth;

using std::placeholders::_1;
using std::placeholders::_2;

namespace bt_intel {
namespace {

void StatusCallback(fxl::Closure complete_cb,
                    bluetooth::hci::CommandChannel::TransactionId id,
                    bluetooth::hci::Status status) {
  std::cout << "  Command Status: " << fxl::StringPrintf("0x%02x", status)
            << " (id=" << id << ")" << std::endl;
  if (status != bluetooth::hci::Status::kSuccess)
    complete_cb();
}

hci::CommandChannel::TransactionId SendCommand(
    const CommandData* cmd_data,
    std::unique_ptr<hci::CommandPacket> packet,
    const hci::CommandChannel::CommandCompleteCallback& cb,
    const fxl::Closure& complete_cb) {
  return cmd_data->cmd_channel()->SendCommand(
      std::move(packet), cmd_data->task_runner(), cb,
      std::bind(&StatusCallback, complete_cb, _1, _2));
}

void LogCommandComplete(hci::Status status,
                        hci::CommandChannel::TransactionId id) {
  std::cout << "  Command Complete - status: "
            << fxl::StringPrintf("0x%02x", status) << " (id=" << id << ")"
            << std::endl;
}

// Prints a byte in decimal and hex forms
std::string PrintByte(uint8_t byte) {
  return fxl::StringPrintf("%u (0x%02x)", byte, byte);
}

std::string EnableParamToString(hci::GenericEnableParam param) {
  return (param == hci::GenericEnableParam::kEnable) ? "enabled" : "disabled";
}

std::string FirmwareVariantToString(uint8_t fw_variant) {
  switch (fw_variant) {
    case 0x06:
      return "bootloader";
    case 0x23:
      return "firmware";
    default:
      break;
  }
  return "UNKNOWN";
}

bool HandleReadVersion(const CommandData* cmd_data,
                       const fxl::CommandLine& cmd_line,
                       const fxl::Closure& complete_cb) {
  if (cmd_line.positional_args().size()) {
    std::cout << "  Usage: read-version [--verbose]" << std::endl;
    return false;
  }

  auto cb = [cmd_line, complete_cb](hci::CommandChannel::TransactionId id,
                                    const hci::EventPacket& event) {
    auto params = event.return_params<IntelVersionReturnParams>();
    LogCommandComplete(params->status, id);

    std::cout << fxl::StringPrintf(
        "  Firmware Summary: variant=%s - revision %u.%u build no: %u (week "
        "%u, year %u)",
        FirmwareVariantToString(params->fw_variant).c_str(),
        params->fw_revision >> 4, params->fw_revision & 0x0F,
        params->fw_build_num, params->fw_build_week,
        2000 + params->fw_build_year);
    std::cout << std::endl;

    if (cmd_line.HasOption("verbose")) {
      std::cout << "  Intel Read Version:" << std::endl;
      std::cout << "    Hardware Platform: " << PrintByte(params->hw_platform)
                << std::endl;
      std::cout << "    Hardware Variant:  " << PrintByte(params->hw_variant)
                << std::endl;
      std::cout << "    Hardware Revision: " << PrintByte(params->hw_revision)
                << std::endl;
      std::cout << "    Firmware Variant:  " << PrintByte(params->fw_variant)
                << std::endl;
      std::cout << "    Firmware Revision: " << PrintByte(params->fw_revision)
                << std::endl;
      std::cout << "    Firmware Build No: " << PrintByte(params->fw_build_num)
                << std::endl;
      std::cout << "    Firmware Build Week: "
                << PrintByte(params->fw_build_week) << std::endl;
      std::cout << "    Firmware Build Year: "
                << PrintByte(params->fw_build_year) << std::endl;
      std::cout << "    Firmware Patch No: " << PrintByte(params->fw_patch_num)
                << std::endl;
    }

    complete_cb();
  };

  auto packet = hci::CommandPacket::New(kReadVersion);
  auto id = SendCommand(cmd_data, std::move(packet), cb, complete_cb);
  std::cout << "  Sent HCI Vendor (Intel) Read Version (id=" << id << ")"
            << std::endl;

  return true;
}

bool HandleReadBootParams(const CommandData* cmd_data,
                          const fxl::CommandLine& cmd_line,
                          const fxl::Closure& complete_cb) {
  if (cmd_line.positional_args().size() || cmd_line.options().size()) {
    std::cout << "  Usage: read-boot-params" << std::endl;
    return false;
  }

  auto cb = [cmd_line, complete_cb](hci::CommandChannel::TransactionId id,
                                    const hci::EventPacket& event) {
    auto params = event.return_params<IntelReadBootParamsReturnParams>();
    LogCommandComplete(params->status, id);

    std::cout << "  Intel Boot Parameters:" << std::endl;
    std::cout << "    Device Revision:  " << le16toh(params->dev_revid)
              << std::endl;
    std::cout << "    Secure Boot:      "
              << EnableParamToString(params->secure_boot) << std::endl;
    std::cout << "    OTP Lock:         "
              << EnableParamToString(params->otp_lock) << std::endl;
    std::cout << "    API Lock:         "
              << EnableParamToString(params->api_lock) << std::endl;
    std::cout << "    Debug Lock:       "
              << EnableParamToString(params->debug_lock) << std::endl;
    std::cout << "    Limited CCE:      "
              << EnableParamToString(params->limited_cce) << std::endl;
    std::cout << "    OTP BD_ADDR:      " << params->otp_bdaddr.ToString()
              << std::endl;
    std::cout << "    Minimum Firmware Build: "
              << fxl::StringPrintf("build no: %u (week %u, year %u)",
                                   params->min_fw_build_num,
                                   params->min_fw_build_week,
                                   2000 + params->min_fw_build_year)
              << std::endl;

    complete_cb();
  };

  auto packet = hci::CommandPacket::New(kReadBootParams);
  auto id = SendCommand(cmd_data, std::move(packet), cb, complete_cb);
  std::cout << "  Sent HCI Vendor (Intel) Read Boot Params (id=" << id << ")"
            << std::endl;

  return true;
}

bool HandleReset(const CommandData* cmd_data,
                 const fxl::CommandLine& cmd_line,
                 const fxl::Closure& complete_cb) {
  if (cmd_line.positional_args().size() || cmd_line.options().size()) {
    std::cout << "  Usage: reset" << std::endl;
    return false;
  }

  auto cb = [](hci::CommandChannel::TransactionId id,
               const hci::EventPacket& event) {};

  auto packet =
      hci::CommandPacket::New(kReset, sizeof(IntelResetCommandParams));
  auto params =
      packet->mutable_view()->mutable_payload<IntelResetCommandParams>();
  params->data[0] = 0x00;
  params->data[1] = 0x01;
  params->data[2] = 0x00;
  params->data[3] = 0x01;
  params->data[4] = 0x00;
  params->data[5] = 0x08;
  params->data[6] = 0x04;
  params->data[7] = 0x00;

  auto id = SendCommand(cmd_data, std::move(packet), cb, complete_cb);
  std::cout << "  Sent HCI Vendor (Intel) Reset (id=" << id << ")" << std::endl;

  // Once the reset command is sent, the hardware will shut down and we won't be
  // able to get a response back. Just exit the tool.
  // TODO(armansito): This needs to be implemented properly in the driver as
  // part of the controller boot sequence. We cannot reboot the controller from
  // userland since the hardware disappears so we'll never receive the
  // vendor-specific HCI event.
  cmd_data->task_runner()->PostDelayedTask(
      complete_cb, fxl::TimeDelta::FromMilliseconds(250));

  return true;
}

}  // namespace

void RegisterCommands(const CommandData* data,
                      bluetooth::tools::CommandDispatcher* dispatcher) {
#define BIND(handler) \
  std::bind(&handler, data, std::placeholders::_1, std::placeholders::_2)

  dispatcher->RegisterHandler("read-version",
                              "Read hardware version information",
                              BIND(HandleReadVersion));
  dispatcher->RegisterHandler("read-boot-params",
                              "Read hardware boot parameters",
                              BIND(HandleReadBootParams));
  dispatcher->RegisterHandler("reset", "Reset firmware", BIND(HandleReset));

#undef BIND
}

}  // namespace bt_intel

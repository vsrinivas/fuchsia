// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "commands.h"

#include <endian.h>
#include <sys/mman.h>

#include <cstring>
#include <iostream>

#include "bt_intel.h"
#include "intel_firmware_loader.h"
#include "src/connectivity/bluetooth/core/bt-host/common/advertising_data.h"
#include "src/connectivity/bluetooth/core/bt-host/common/manufacturer_names.h"
#include "src/connectivity/bluetooth/core/bt-host/hci/advertising_report_parser.h"
#include "src/connectivity/bluetooth/core/bt-host/transport/command_channel.h"
#include "src/connectivity/bluetooth/core/bt-host/transport/control_packets.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/lib/fxl/strings/string_printf.h"

using bt::hci::CommandPacket;
using bt::hci::EventPacket;
using bt::hci_spec::GenericEnableParam;
using bt::hci_spec::StatusCode;

using std::placeholders::_1;
using std::placeholders::_2;

namespace bt_intel {
namespace {

class MfgModeEnabler {
 public:
  MfgModeEnabler(CommandChannel* channel) : channel_(channel), patch_reset_needed_(false) {
    auto packet = MakeMfgModePacket(true);
    channel_->SendCommandSync(packet->view(), [](const auto& evt) {
      std::cout << fxl::StringPrintf("Mfg mode enable complete (status: 0x%02x)", evt.event_code())
                << std::endl;
    });
  }

  ~MfgModeEnabler() {
    MfgDisableMode disable_mode = MfgDisableMode::kNoPatches;
    if (patch_reset_needed_) {
      std::cout << "Patches will be enabled" << std::endl;
      disable_mode = MfgDisableMode::kPatchesEnabled;
    }

    auto packet = MakeMfgModePacket(false, disable_mode);
    channel_->SendCommandSync(packet->view(), [](const auto& evt) {
      std::cout << fxl::StringPrintf("Mfg mode disable complete (status: 0x%02x)", evt.event_code())
                << std::endl;
    });
  }

  void set_patch_reset(bool patch) { patch_reset_needed_ = patch; }

 private:
  CommandChannel* channel_;
  bool patch_reset_needed_;

  std::unique_ptr<CommandPacket> MakeMfgModePacket(
      bool enable, MfgDisableMode disable_mode = MfgDisableMode::kNoPatches) {
    auto packet = CommandPacket::New(kMfgModeChange, sizeof(IntelMfgModeChangeCommandParams));
    auto params = packet->mutable_payload<IntelMfgModeChangeCommandParams>();
    params->enable = enable ? GenericEnableParam::ENABLE : GenericEnableParam::DISABLE;
    params->disable_mode = disable_mode;
    return packet;
  }
};

void LogCommandComplete(StatusCode status) {
  std::cout << "  Command Complete - status: " << fxl::StringPrintf("0x%02x", status) << std::endl;
}

// Prints a byte in decimal and hex forms
std::string PrintByte(uint8_t byte) { return fxl::StringPrintf("%u (0x%02x)", byte, byte); }

std::string EnableParamToString(GenericEnableParam param) {
  return (param == GenericEnableParam::ENABLE) ? "enabled" : "disabled";
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

bool HandleReadVersion(CommandChannel* cmd_channel, const fxl::CommandLine& cmd_line,
                       const fit::closure& complete_cb) {
  if (cmd_line.positional_args().size()) {
    std::cout << "  Usage: read-version [--verbose]" << std::endl;
    return false;
  }

  auto cb = [cmd_line](const EventPacket& event) {
    auto params = event.return_params<IntelVersionReturnParams>();
    LogCommandComplete(params->status);

    std::cout << fxl::StringPrintf(
        "  Firmware Summary: variant=%s - revision %u.%u build no: %u (week "
        "%u, year %u)",
        FirmwareVariantToString(params->fw_variant).c_str(), params->fw_revision >> 4,
        params->fw_revision & 0x0F, params->fw_build_num, params->fw_build_week,
        2000 + params->fw_build_year);
    std::cout << std::endl;

    if (cmd_line.HasOption("verbose")) {
      std::cout << "  Intel Read Version:" << std::endl;
      std::cout << "    Hardware Platform: " << PrintByte(params->hw_platform) << std::endl;
      std::cout << "    Hardware Variant:  " << PrintByte(params->hw_variant) << std::endl;
      std::cout << "    Hardware Revision: " << PrintByte(params->hw_revision) << std::endl;
      std::cout << "    Firmware Variant:  " << PrintByte(params->fw_variant) << std::endl;
      std::cout << "    Firmware Revision: " << PrintByte(params->fw_revision) << std::endl;
      std::cout << "    Firmware Build No: " << PrintByte(params->fw_build_num) << std::endl;
      std::cout << "    Firmware Build Week: " << PrintByte(params->fw_build_week) << std::endl;
      std::cout << "    Firmware Build Year: " << PrintByte(params->fw_build_year) << std::endl;
      std::cout << "    Firmware Patch No: " << PrintByte(params->fw_patch_num) << std::endl;
    }
  };

  auto packet = CommandPacket::New(kReadVersion);
  std::cout << "  Sending HCI Vendor (Intel) Read Version" << std::endl;
  cmd_channel->SendCommandSync(packet->view(), cb);

  return false;
}

bool HandleReadBootParams(CommandChannel* cmd_channel, const fxl::CommandLine& cmd_line,
                          fit::closure complete_cb) {
  if (cmd_line.positional_args().size() || cmd_line.options().size()) {
    std::cout << "  Usage: read-boot-params" << std::endl;
    return false;
  }

  auto cb = [](const EventPacket& event) {
    auto params = event.return_params<IntelReadBootParamsReturnParams>();
    LogCommandComplete(params->status);

    std::cout << "  Intel Boot Parameters:" << std::endl;
    std::cout << "    Device Revision:  " << le16toh(params->dev_revid) << std::endl;
    std::cout << "    Secure Boot:      " << EnableParamToString(params->secure_boot) << std::endl;
    std::cout << "    OTP Lock:         " << EnableParamToString(params->otp_lock) << std::endl;
    std::cout << "    API Lock:         " << EnableParamToString(params->api_lock) << std::endl;
    std::cout << "    Debug Lock:       " << EnableParamToString(params->debug_lock) << std::endl;
    std::cout << "    Limited CCE:      " << EnableParamToString(params->limited_cce) << std::endl;
    std::cout << "    OTP BD_ADDR:      " << params->otp_bdaddr.ToString() << std::endl;
    std::cout << "    Minimum Firmware Build: "
              << fxl::StringPrintf("build no: %u (week %u, year %u)", params->min_fw_build_num,
                                   params->min_fw_build_week, 2000 + params->min_fw_build_year)
              << std::endl;
  };

  auto packet = CommandPacket::New(kReadBootParams);
  std::cout << "  Sending HCI Vendor (Intel) Read Boot Params" << std::endl;
  cmd_channel->SendCommandSync(packet->view(), cb);

  return false;
}

bool HandleReset(CommandChannel* cmd_channel, const fxl::CommandLine& cmd_line,
                 fit::closure complete_cb) {
  if (cmd_line.positional_args().size() || cmd_line.options().size()) {
    std::cout << "  Usage: reset" << std::endl;
    return false;
  }

  auto packet = CommandPacket::New(kReset, sizeof(IntelResetCommandParams));
  auto params = packet->mutable_payload<IntelResetCommandParams>();
  params->data[0] = 0x00;
  params->data[1] = 0x01;
  params->data[2] = 0x00;
  params->data[3] = 0x01;
  params->data[4] = 0x00;
  params->data[5] = 0x08;
  params->data[6] = 0x04;
  params->data[7] = 0x00;

  cmd_channel->SendCommand(packet->view());
  std::cout << "  Sent HCI Vendor (Intel) Reset" << std::endl;

  // Once the reset command is sent, the hardware will shut down and we won't
  // get a response back. Just exit the tool.

  // TODO(jamuraa): When rebooting, the Intel firmware actually sends a special
  // vendor firmware event (0xff) indicating boot has completed.  Process this
  // event instead on a reset.

  return false;
}

bool HandleLoadBseq(CommandChannel* cmd_channel, const fxl::CommandLine& cmd_line,
                    fit::closure complete_cb) {
  if (cmd_line.positional_args().size() != 1) {
    std::cout << "  Usage: load-bseq [--verbose] <filename>" << std::endl;
    return false;
  }

  std::string firmware_fn = cmd_line.positional_args().front();

  {
    MfgModeEnabler enable(cmd_channel);

    IntelFirmwareLoader loader(cmd_channel);

    IntelFirmwareLoader::LoadStatus patched = loader.LoadBseq(firmware_fn);

    if (patched == IntelFirmwareLoader::LoadStatus::kPatched) {
      enable.set_patch_reset(true);
    }
  }

  return false;
}

bool HandleLoadSecure(CommandChannel* cmd_channel, const fxl::CommandLine& cmd_line,
                      fit::closure complete_cb) {
  if (cmd_line.positional_args().size() != 1) {
    std::cout << "  Usage: load-sfi [--verbose] <filename>" << std::endl;
    return false;
  }

  std::string firmware_fn = cmd_line.positional_args().front();

  IntelFirmwareLoader loader(cmd_channel);

  loader.LoadSfi(firmware_fn);

  return false;
}

}  // namespace

void RegisterCommands(CommandChannel* data, ::bluetooth_tools::CommandDispatcher* dispatcher) {
#define BIND(handler) std::bind(&handler, data, std::placeholders::_1, std::placeholders::_2)

  dispatcher->RegisterHandler("read-version", "Read hardware version information",
                              BIND(HandleReadVersion));
  dispatcher->RegisterHandler("read-boot-params", "Read hardware boot parameters",
                              BIND(HandleReadBootParams));
  dispatcher->RegisterHandler("load-bseq", "Load bseq file onto device", BIND(HandleLoadBseq));
  dispatcher->RegisterHandler("load-sfi", "Load Secure Firmware onto device",
                              BIND(HandleLoadSecure));
  dispatcher->RegisterHandler("reset", "Reset firmware", BIND(HandleReset));

#undef BIND
}

}  // namespace bt_intel
